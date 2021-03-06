// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.
#include "Framework/ConfigParamSpec.h"
#include <boost/program_options.hpp>

#include <string>
#include <vector>

namespace bpo = boost::program_options;

namespace o2 {
namespace framework {

void populateBoostProgramOptions(
    bpo::options_description &options,
    const std::vector<ConfigParamSpec> &specs
  ) {
  auto proxy = options.add_options();
  for (auto & spec : specs) {
    const char *name = spec.name.c_str();
    const char *help = spec.help.c_str();

    switch(spec.type) {
      // FIXME: Should we handle int and size_t diffently?
      // FIXME: We should probably raise an error if the type is unknown
      case VariantType::Int:
      case VariantType::Int64:
        proxy = proxy(name, bpo::value<int>()->default_value(spec.defaultValue.get<int>()), help);
        break;
      case VariantType::Float:
        proxy = proxy(name, bpo::value<float>()->default_value(spec.defaultValue.get<float>()), help);
        break;
      case VariantType::Double:
        proxy = proxy(name, bpo::value<double>()->default_value(spec.defaultValue.get<double>()), help);
        break;
      case VariantType::String:
        proxy = proxy(name, bpo::value<std::string>()->default_value(spec.defaultValue.get<const char *>()), help);
        break;
      case VariantType::Bool:
        proxy = proxy(name, bpo::value<bool>()->default_value(spec.defaultValue.get<bool>()), help);
        break;
      case VariantType::Unknown:
        break;
    };
  }
}

}
}
