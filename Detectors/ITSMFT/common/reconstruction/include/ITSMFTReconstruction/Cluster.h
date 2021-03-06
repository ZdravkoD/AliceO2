// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file Cluster.h
/// \brief Definition of the ITSMFT cluster
#ifndef ALICEO2_ITSMFT_CLUSTER_H
#define ALICEO2_ITSMFT_CLUSTER_H

//#include "FairTimeStamp.h" // for FairTimeStamp
#include "DetectorsBase/BaseCluster.h"
#include "SimulationDataFormat/MCCompLabel.h"

// uncomment this to have cluster topology in stored
//#define _ClusterTopology_

#define CLUSTER_VERSION 2

namespace o2
{
namespace ITSMFT
{
  class GeometryTGeo;
/// \class Cluster
/// \brief Cluster class for the ITSMFT
///

class Cluster : public o2::Base::BaseCluster<float>
{
  using Label = o2::MCCompLabel;
  
 public:
  enum { // frame in which the track is currently defined
    kUsed,
    kShared
  };
  //
  enum {
    kOffsNZ = 0,
    kMaskNZ = 0xff,
    kOffsNX = 8,
    kMaskNX = 0xff,
    kOffsNPix = 16,
    kMaskNPix = 0x1ff,
    kOffsClUse = 25,
    kMaskClUse = 0x7f
  };
//
#ifdef _ClusterTopology_
  enum { kMaxPatternBits = 32 * 16, kMaxPatternBytes = kMaxPatternBits / 8, kSpanMask = 0x7fff,
         kTruncateMask = 0x8000 };
#endif
  using BaseCluster::BaseCluster;

 public:
  static constexpr int maxLabels=3;

  ~Cluster() override = default;

  Cluster& operator=(const Cluster& cluster) = delete; // RS why?

  //****** Basic methods ******************
  void setLabel(Label lab, Int_t i)
  {
    if (i >= 0 && i < maxLabels)
      mTracks[i] = lab;
  }

  Label   getLabel(Int_t i) const { return mTracks[i]; }

  void setUsed()                { setBit(kUsed);}
  void setShared()              { setBit(kShared);}
  void increaseClusterUsage()   { isUsed() ? setBit(kShared) : setBit(kUsed); }
  //
  bool isUsed()   const { return isBitSet(kUsed); }
  bool isShared() const { return isBitSet(kShared); }
  //
  void setNxNzN(UChar_t nx, UChar_t nz, UShort_t n)
  {
    mNxNzN = ((n & kMaskNPix) << kOffsNPix) + ((nx & kMaskNX) << kOffsNX) + ((nz & kMaskNZ) << kOffsNZ);
  }
  void setClusterUsage(int n);
  void modifyClusterUsage(bool used = kTRUE) { used ? incClusterUsage() : decreaseClusterUsage(); }
  void incClusterUsage()
  {
    setClusterUsage(getClusterUsage() + 1);
    increaseClusterUsage();
  }
  void decreaseClusterUsage();
  int getNx() const { return (mNxNzN >> kOffsNX) & kMaskNX; }
  int getNz() const { return (mNxNzN >> kOffsNZ) & kMaskNZ; }
  int getNPix() const { return (mNxNzN >> kOffsNPix) & kMaskNPix; }
  int getClusterUsage() const { return (mNxNzN >> kOffsClUse) & kMaskClUse; }
  //
  UInt_t getROFrame()         const {return mROFrame;}
  void   setROFrame(UInt_t v)       {mROFrame = v;}
  //
  bool hasCommonTrack(const Cluster* cl) const;
  //
#ifdef _ClusterTopology_
  int  getPatternRowSpan() const { return mPatternNRows & kSpanMask; }
  int  getPatternColSpan() const { return mPatternNCols & kSpanMask; }
  bool isPatternRowsTruncated() const { return mPatternNRows & kTruncateMask; }
  bool isPatternColsTruncated() const { return mPatternNRows & kTruncateMask; }
  bool isPatternTruncated() const { return isPatternRowsTruncated() || isPatternColsTruncated(); }
  void setPatternRowSpan(UShort_t nr, bool truncated);
  void setPatternColSpan(UShort_t nc, bool truncated);
  void setPatternMinRow(UShort_t row) { mPatternMinRow = row; }
  void setPatternMinCol(UShort_t col) { mPatternMinCol = col; }
  void resetPattern();
  bool testPixel(UShort_t row, UShort_t col) const;
  void setPixel(UShort_t row, UShort_t col, bool fired = kTRUE);
  void getPattern(UChar_t patt[kMaxPatternBytes])
  {
    for (int i=kMaxPatternBytes; i--;)
      patt[i] = mPattern[i];
  }
  int getPatternMinRow() const { return mPatternMinRow; }
  int getPatternMinCol() const { return mPatternMinCol; }
#endif
  //
 protected:
  //
  Label mTracks[maxLabels];   ///< MC labels
  UInt_t  mROFrame;   ///< RO Frame
  Int_t mNxNzN=0;          ///< effective cluster size in X (1st byte) and Z (2nd byte) directions
                           ///< and total Npix(next 9 bits).
                           ///> The last 7 bits are used for clusters usage counter

#ifdef _ClusterTopology_
  UShort_t mPatternNRows = 0;             ///< pattern span in rows
  UShort_t mPatternNCols = 0;             ///< pattern span in columns
  UShort_t mPatternMinRow = 0;            ///< pattern start row
  UShort_t mPatternMinCol = 0;            ///< pattern start column
  UChar_t mPattern[kMaxPatternBytes] = {0}; ///< cluster topology
  //
  ClassDefOverride(Cluster, CLUSTER_VERSION + 1)
#else
  ClassDefOverride(Cluster, CLUSTER_VERSION)
#endif
};
//______________________________________________________
inline void Cluster::decreaseClusterUsage()
{
  // decrease cluster usage counter
  int n = getClusterUsage();
  if (n)
    setClusterUsage(--n);
  //
}

//______________________________________________________
inline void Cluster::setClusterUsage(Int_t n)
{
  // set cluster usage counter
  mNxNzN &= ~(kMaskClUse << kOffsClUse);
  mNxNzN |= (n & kMaskClUse) << kOffsClUse;
  if (n < 2) resetBit(kShared);
  if (!n)    resetBit(kUsed);
}
 
}
}

#endif /* ALICEO2_ITSMFT_CLUSTER_H */
