// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.
#include "Framework/DeviceSpec.h"
#include "Framework/WorkflowSpec.h"
#include "Framework/ChannelMatching.h"
#include "Framework/DeviceControl.h"
#include <vector>

using namespace o2::framework;

namespace o2 {
namespace framework {

// Construct the list of actual devices we want, given a workflow.
void
dataProcessorSpecs2DeviceSpecs(const o2::framework::WorkflowSpec &workflow,
                               std::vector<o2::framework::DeviceSpec> &devices) {
  // FIXME: for the moment we assume one channel per kind of product. Later on we
  //        should optimize and make sure that multiple products can be multiplexed
  //        on a single channel in case of a single receiver.
  // FIXME: make start port configurable?
  std::map<LogicalChannel, int> maxOutputCounts; // How many outputs of a given kind do we need?
  // First we calculate the available outputs
  for (auto &spec : workflow) {
    for (auto &out : spec.outputs) {
      maxOutputCounts.insert(std::make_pair(outputSpec2LogicalChannel(out), 0));
    }
  }

  // Then we calculate how many inputs match a given output.
  for (auto &output: maxOutputCounts) {
    for (auto &spec : workflow) {
      for (auto &in : spec.inputs) {
        if (matchDataSpec2Channel(in, output.first)) {
          maxOutputCounts[output.first] += 1;
        }
      }
    }
  }

  // We then create the actual devices.
  // - If an input is used only once, we simply connect to the output.
  // - If an input is used by multiple entries, we keep track of how
  //   how many have used it so far and make sure that all but the last
  //   device using such an output forward it as `out_<origin>_<description>_<N>`.
  // - If there is no output matching a given input, we should complain
  // - If there is no input using a given output, we should warn?
  std::map<PhysicalChannel, short> portMappings;
  unsigned short nextPort = 22000;
  std::map<LogicalChannel, size_t> outputUsages;

  for (auto &processor : workflow) {
    DeviceSpec device;
    device.id = processor.name;
    device.algorithm = processor.algorithm;
    device.options = processor.options;

    // Channels which need to be forwarded (because they are used by
    // a downstream provider).
    std::vector<ChannelSpec> forwardedChannels;

    for (auto &outData : processor.outputs) {
      ChannelSpec channel;
      channel.method = Bind;
      channel.type = Pub;
      channel.port = nextPort;

      auto logicalChannel = outputSpec2LogicalChannel(outData);
      auto physicalChannel = outputSpec2PhysicalChannel(outData, 0);
      auto channelUsage = outputUsages.find(logicalChannel);
      // Decide the name of the channel. If this is
      // the first time this channel is used, it means it
      // is really an output.
      if (channelUsage != outputUsages.end()) {
        throw std::runtime_error("Too many outputs with the same name");
      }

      outputUsages.insert(std::make_pair(logicalChannel, 0));
      auto portAlloc = std::make_pair(physicalChannel, nextPort);
      channel.name = physicalChannel.id;

      device.channels.push_back(channel);
      device.outputs.insert(std::make_pair(channel.name, outData));

      // This should actually be implied by the previous assert.
      assert(portMappings.find(portAlloc.first) == portMappings.end());
      portMappings.insert(portAlloc);
      nextPort++;
    }

    // We now process the inputs. They are all of connect kind and we
    // should look up in the previously created channel map where to
    // connect to. If we have more than one device which reads
    // from the same output, we should actually treat them as
    // serialised output.
    // FIXME: Alexey was referring to a device which can duplicate output
    //        without overhead (not sure how that would work). Maybe we should use
    //        that instead.
    for (auto &input : processor.inputs) {
      ChannelSpec channel;
      channel.method = Connect;
      channel.type = Sub;

      // Create the channel name and find how many times we have used it.
      auto logicalChannel = inputSpec2LogicalChannelMatcher(input);
      auto usagesIt = outputUsages.find(logicalChannel);
      if (usagesIt == outputUsages.end()) {
        throw std::runtime_error("Could not find output matching " + logicalChannel.name);
      }

      // Find the maximum number of usages for the channel.
      auto maxUsagesIt = maxOutputCounts.find(logicalChannel);
      if (maxUsagesIt == maxOutputCounts.end()) {
        // The previous throw should already catch this condition.
        assert(false && "The previous check should have already caught this");
      }
      auto maxUsages = maxUsagesIt->second;

      // Create the input channels:
      // - Name of the channel to lookup always channel_name_<usages>
      // - If usages is different from maxUsages, we should create a forwarding
      // channel.
      auto logicalInput = inputSpec2LogicalChannelMatcher(input);
      auto currentChannelId = outputUsages.find(logicalInput);
      if (currentChannelId == outputUsages.end()) {
        std::runtime_error("Missing output for " + logicalInput.name);
      }

      auto physicalChannel = inputSpec2PhysicalChannelMatcher(input, currentChannelId->second);
      auto currentPort = portMappings.find(physicalChannel);
      if (currentPort == portMappings.end()) {
        std::runtime_error("Missing physical channel " + physicalChannel.id);
      }

      channel.name = "in_" + physicalChannel.id;
      channel.port = currentPort->second;
      device.channels.push_back(channel);
      device.inputs.insert(std::make_pair(channel.name, input));
      // Increase the number of usages we did for a given logical channel
      currentChannelId->second += 1;

      // Here is where we create the forwarding port which can be later reused.
      if (currentChannelId->second != maxUsages) {
        ChannelSpec forwardedChannel;
        forwardedChannel.method = Bind;
        forwardedChannel.type = Pub;
        auto physicalForward = inputSpec2PhysicalChannelMatcher(input, currentChannelId->second);
        forwardedChannel.port = nextPort;
        portMappings.insert(std::make_pair(physicalForward, nextPort));
        forwardedChannel.name = physicalForward.id;

        device.channels.push_back(forwardedChannel);
        device.forwards.insert(std::make_pair(forwardedChannel.name, input));
        nextPort++;
      }
    }
    devices.push_back(device);
  }
}

/// This creates a string to configure channels of a FairMQDevice
/// FIXME: support shared memory
std::string channel2String(const ChannelSpec &channel) {
  std::string result;
  char buffer[32];
  auto addressFormat = (channel.method == Bind ? "tcp://*:%d" : "tcp://127.0.0.1:%d");

  result += "name=" + channel.name + ",";
  result += std::string("type=") + (channel.type == Pub ? "pub" : "sub") + ",";
  result += std::string("method=") + (channel.method == Bind ? "bind" : "connect") + ",";
  result += std::string("address=") + (snprintf(buffer,32,addressFormat, channel.port), buffer);

  return result;
}

void
prepareArguments(int argc,
                 char **argv,
                 bool defaultQuiet,
                 bool defaultStopped,
                 std::vector<DeviceSpec> &deviceSpecs,
                 std::vector<DeviceControl> &deviceControls)
{
  for (size_t si = 0; si < deviceSpecs.size(); ++si) {
    auto &spec = deviceSpecs[si];
    auto &control = deviceControls[si];
    control.quiet = defaultQuiet;
    control.stopped = defaultStopped;

    // We duplicate the list of options, filtering only those
    // which are actually relevant for the given device. The additional
    // four are to add
    // * name of the executable
    // * --framework-id <id> so that we can use the workflow
    //   executable also in other context where we do not fork, e.g. DDS.
    // * final NULL required by execvp
    //
    // We do it here because we are still in the parent and we can therefore
    // capture them to be displayed in the GUI or to populate the DDS configuration
    // to dump

    // Set up options for the device running underneath
    // FIXME: add some checksum in framework id. We could use this
    //        to avoid redeploys when only a portion of the workflow is changed.
    // FIXME: this should probably be done in one go with char *, but I am lazy.
    std::vector<std::string> tmpArgs = {
      argv[0],
      "--id",
      spec.id.c_str(),
      "--control",
      "static",
      "--log-color",
      "0"
    };

    // Do filtering. Since we should only have few options,
    // FIXME: finish here...
    for (size_t ai = 0; ai < argc; ++ai) {
      for (size_t oi = 0; oi < spec.options.size(); ++oi) {
        char *currentOpt = argv[ai];
        LOG(DEBUG) << "Checking if " << currentOpt << " is needed by " << spec.id;
        if (ai + 1 > argc) {
          std::cerr << "Missing value for " << currentOpt;
          exit(1);
        }
        char *currentOptValue = argv[ai+1];
        const std::string &validOption = "--" + spec.options[oi].name;
        if (strncmp(currentOpt, validOption.c_str(), validOption.size()) == 0) {
          tmpArgs.emplace_back(strdup(validOption.c_str()));
          tmpArgs.emplace_back(strdup(currentOptValue));
          control.options.insert(std::make_pair(spec.options[oi].name,
                                                currentOptValue));
          break;
        }
      }
    }

    // Add the channel configuration
    for (auto &channel : spec.channels) {
      tmpArgs.emplace_back(std::string("--channel-config"));
      tmpArgs.emplace_back(channel2String(channel));
    }

    // We create the final option list, depending on the channels
    // which are present in a device.
    for (auto &arg : tmpArgs) {
      spec.args.emplace_back(strdup(arg.c_str()));
    }
    // execvp wants a NULL terminated list.
    spec.args.push_back(nullptr);

    //FIXME: this should probably be reflected in the GUI
    std::ostringstream str;
    for (size_t ai = 0; ai < spec.args.size() - 1; ai++) {
      assert(spec.args[ai]);
      str << " " << spec.args[ai];
    }
    LOG(DEBUG) << "The following options are being forwarded to "
               << spec.id << ":" << str.str();
  }
}

} // namespace framework
} // namespace o2
