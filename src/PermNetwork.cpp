/* Copyright (C) 2012,2013 IBM Corp.
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <NTL/ZZ.h>
#include "Ctxt.h"
#include "permutations.h"
#include "EncryptedArray.h"

void PermNetwork::BuildNetwork(const Permut& pi, const GeneratorTrees& trees)
{
  Vec<long> dims;
  trees.getCubeDims(dims);

  std::cerr << "pi =      "<<pi<<endl;
  std::cerr << "map2cube ="<<trees.Map2Cube()<<endl;
  std::cerr << "map2array="<<trees.Map2Array()<<endl;

  // Compute the permutation on the cube, rho = map2cube o pi o map2array
  Permut rho;
  ApplyPermsToFunc(rho, trees.Map2Cube(), pi, trees.Map2Array());
  std::cerr << "rho =     "<<rho<<endl;

  // Break rho along the different dimensions
  CubeSignature sig(dims); // make a cube-signature object
  vector<ColPerm> perms;
  breakPermByDim(perms, rho, sig);

  for (long i=0; i<(long)perms.size(); i++) { // debugging printouts
    Permut tmp;
    perms[i].makeExplicit(tmp);
    std::cerr << " prems["<<i<<"]="<<tmp<<endl;
  }

  // Compute the total number of levels in the target permutation network
  long nLvls=0;
  for (long g=0; g<trees.length(); g++) {
    const OneGeneratorTree &T = trees[g];
    for (long i=0, leaf=T.FirstLeaf(); leaf>=0; i++, leaf=T.NextLeaf(leaf)) {
      long lvls=1; // how many levels to add for this leaf

      const SubDimension& leafData = T[leaf].getData();
      if (leafData.benes != NULL) // a benes leaf
	lvls = 3; /* FIXME: leafData.benes->getNLvls(); */

      if (g<trees.length()-1 || T.NextLeaf(leaf)>=0) 
	lvls *= 2; // not last leaf in last tree, so not middle level
      nLvls += lvls;
    }
  }
  layers.SetLength(nLvls); // allocate space

  // Go over the different permutations and build the corresponding levels
  Vec<long> shifts;
  long dimIdx=0, lyrIdx=0;
  for (long g=0; g<trees.length(); g++) { // go over all the generators/trees
    const OneGeneratorTree &T = trees[g];
    // In each tree, go over all the leaves
    for (long leaf=T.FirstLeaf(); leaf>=0; leaf=T.NextLeaf(leaf)) {
      const SubDimension& leafData = T[leaf].getData();

      if (leafData.benes != NULL) { // A Benes leaf
	// FIXME: do something here..
	lyrIdx += 3; /* FIXME: leafData.benes->getNLvls(); */
	continue;
      }

      // This leaf corresponds to <= permutations idx, n-idx
      const ColPerm& p1 = perms[dimIdx];
      const ColPerm& p2 = perms[perms.size()-dimIdx-1];

      // A naive permutation leaf, corresponding to 1 or 2 levels
      PermNetLayer& l1 = layers[lyrIdx];
      l1.genIdx = g;
      l1.isID = !p1.getShiftAmounts(shifts);
      if (!l1.isID) {
	std::cerr << "layer "<<lyrIdx<<": "<<shifts<<endl;
	if (leafData.good) // For good leaves, shift by -x is the same as size-x
	  for (long k=0; k<shifts.length(); k++)
	    if (shifts[k]<0) shifts[k] += leafData.size;
	// li.shifts[i] := shifts[map2cube[i]]
	ApplyPermToFunc(l1.shifts, shifts, trees.Map2Cube());
	std::cerr << "       : "<<l1.shifts<<endl;
      }
      else std::cerr << "layer "<<lyrIdx<<"= identity\n";

      if (lyrIdx == layers.length()-lyrIdx-1) break;// middle layer (last leaf)
      lyrIdx = layers.length()-lyrIdx-1; // the other layers for this leaf

      PermNetLayer& l2 = layers[lyrIdx];
      l2.genIdx = g;
      l2.isID = !p2.getShiftAmounts(shifts);
      if (!l2.isID) {
	std::cerr << "layer "<<lyrIdx<<": "<<shifts<<endl;
	if (leafData.good) // For good leaves, shift by -x is the same as size-x
	  for (long k=0; k<shifts.length(); k++)
	    if (shifts[k]<0) shifts[k] += leafData.size;
	// li.shifts[i] := shifts[map2cube[i]]
	ApplyPermToFunc(l2.shifts, shifts, trees.Map2Cube());
	std::cerr << "       : "<<l2.shifts<<endl;
      }
      else std::cerr << "layer "<<lyrIdx<<"= identity\n";

      lyrIdx = layers.length()-lyrIdx;
    }
  }
}

void PermNetwork::ApplyToArray(HyperCube<long>& v)
{
}

void PermNetwork::ApplyToPtxt(ZZX& p, const EncryptedArray& ea)
{
}

// Upon return, mask[i]=1 if haystack[i]=needle, 0 otherwise. Also set to 0
// all the entries in haystack where mask[i]=1. Return the index of the first
// nonzero entry in haystack at the end of the pass (-1 if they are all zero).
// Also return a flag saying if any entries of the mask are nonzero.
static pair<long,bool>
makeMask(vector<long>& mask, Vec<long>& haystack, long needle)
{
  long found = false;
  long fstNZidx = -1;
  for (long i=0; i<(long)mask.size(); i++) {
    if (haystack[i] == needle) {
      mask[i]=1;
      haystack[i]=0;
      found = true;
    } 
    else {
      mask[i]=0;
      if (haystack[i]!=0 && fstNZidx<0)
	fstNZidx = i;
    }
  }
  return std::make_pair(fstNZidx,found);
}

void PermNetwork::ApplyToCtxt(Ctxt& c)
{
  const PAlgebra& al = c.getContext().zMStar;
  EncryptedArray ea(c.getContext()); // use G(X)=X for this ea object

  // Apply the layers, one at a time
  for (long i=0; i<layers.length(); i++) {
    const PermNetLayer& lyr = layers[i];
    if (lyr.isID) continue; // this layer is the identity permutation

    // This layer is shifted via powers of g^e mod m
    long g2e = PowerMod(al.ZmStarGen(lyr.genIdx), lyr.e, al.getM());

    Vec<long> unused = lyr.shifts;
    vector<long> mask(lyr.shifts.length());  // buffer to hold masks
    Ctxt sum(c.getPubKey(), c.getPtxtSpace()); // an empty ciphertext

    long shamt = 0;
    bool frst = true;
    while (true) {
      pair<long,bool> ret=makeMask(mask, unused, shamt); // compute mask
      if (ret.second) { // non-empty mask
	Ctxt tmp = c;
	ZZX maskPoly;
	ea.encode(maskPoly, mask);    // encode mask as polynomial
	tmp.multByConstant(maskPoly); // multiply by mask
	if (shamt!=0) // rotate if the shift amount is nonzero
	  tmp.smartAutomorph(PowerMod(g2e, shamt, al.getM()));
	if (frst) {
	  sum = tmp;
	  frst = false;
	}
	else
	  sum += tmp;
      }
      if (ret.first >= 0)
	shamt = unused[ret.first]; // next shift amount to use

      else break; // unused is all-zero, done with this layer
    }
    c = sum; // update the cipehrtext c before the next layer
  }
}