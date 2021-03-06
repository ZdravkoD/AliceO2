// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "runFairMQDevice.h"
#include "Publishers/DataPublisherDevice.h"

namespace bpo = boost::program_options;

void addCustomOptions(bpo::options_description& options)
{
  options.add_options()
    (o2::Utilities::DataPublisherDevice::OptionKeyDataDescription,
     bpo::value<std::string>()->default_value("unspecified"),
     "default data description")
    (o2::Utilities::DataPublisherDevice::OptionKeyDataOrigin,
     bpo::value<std::string>()->default_value("void"),
     "default data origin")
    (o2::Utilities::DataPublisherDevice::OptionKeySubspecification,
     bpo::value<o2::Utilities::DataPublisherDevice::SubSpecificationT>()->default_value(~(o2::Utilities::DataPublisherDevice::SubSpecificationT)0),
     "default sub specification")
    (o2::Utilities::DataPublisherDevice::OptionKeyFileName,
     bpo::value<std::string>()->default_value(""),
     "File name")
    (o2::Utilities::DataPublisherDevice::OptionKeyInputChannelName,
     bpo::value<std::string>()->default_value("input"),
     "Name of the input channel")
    (o2::Utilities::DataPublisherDevice::OptionKeyOutputChannelName,
     bpo::value<std::string>()->default_value("output"),
     "Name of the output channel");
}

FairMQDevicePtr getDevice(const FairMQProgOptions& /*config*/)
{
  return new o2::Utilities::DataPublisherDevice();
}
