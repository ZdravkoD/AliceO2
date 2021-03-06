// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.
#ifndef FRAMEWORK_DDSCONFIGHELPERS_H
#define FRAMEWORK_DDSCONFIGHELPERS_H

#include "Framework/DeviceSpec.h"
#include <vector>
#include <iosfwd>

namespace o2 {
namespace framework {

/// Helper to dump DDS configuration to run in a deployed
/// manner.
/// @a out is a stream where the configuration will be printed
/// @a specs is the internal representation of the dataflow topology
///          which we want to dump.
void dumpDeviceSpec2DDS(std::ostream &out, const std::vector<DeviceSpec> &specs);

} // namespace framework
} // namespace o2
#endif // FRAMEWORK_DDSCONFIGHELPERS_H
