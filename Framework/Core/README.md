# Data Processing Layer in O2 Framework

# Status quo and motivation for an O2 Data Processing Layer

FairMQ currently provides a well documented and flexible framework for an actor based computation where each of the actors listens for message-like entities on channels and executes some code as a reaction. The key component which controls this is called a `FairMQDevice` (or *device* from now on) which can use different kind of transports to receive and send messages. In the most generic case, users are allowed to have full control on the state machine governing the message passing and have complete control on how the message handling is done. This of course covers all ALICE usecases in a generic manner, at the cost of extra complexity left to the user to implement. In most cases however a simplified way of creating devices is provided, and the user will simply create its own `FairMQDevice`-derived class, which registers via the `OnData(FairMQParts &parts)` method a callback that is invoked whenever a new message arrives. This however still holds the user responsible for:


- Verifying that the required inputs for the computation are all available, both from the actual data flow (being it for readout, reconstruction or analysis) and from the asynchronous stream (e.g. alignment and calibrations).
- Create the appropriate message which holds the results and send it.
- Ensure the ease of testability, the code reuse and the proper documentation `OnData` callback. In particular there is no way to inspect which data is expected by a device and which data is produced.

This is by design, because the FairMQ transport layer should not know anything about the actual data being transferred, while all the points above require some sort of inner knowledge about the data model and the data being moved around.

# Separating data-processing from transport

For the reasons mentioned above, we propose that the one of the developments which should happen with the O2 Framework work package is the development of a “Data Processing layer” which actually knows about the O2 Data Model (and for this reason cannot be part of FairMQ itself) and exploits it to validate, optimise and correctly schedule a computation on a user specified set of inputs.

## Describing a computation

The description of the computation in such a layer would be defined via  a `DataProcessor``Spec` class, which describes some sort of processing of a (set of) O2 Data Payloads (*payloads* from now on), as defined by the O2 Data Model,  eventually producing new payloads as outputs. Both the inputs to the computation and the outputs should be specified in the `DataProcessorSpec` instance and communicated to a `DataProcessingManager` instance which configures the processing device accordingly. Practically speaking this would translate into something similar to the current “simplified device mode” where the user includes a special header which contains all the boilerplate and provides a well defined callback where the requested `DataProcessorSpec` are provided.

The `DataProcessorSpec` is defined as follows:


    struct DataProcessorSpec {
       using InitCallback = std::function<ProcessCallback(const ConfigParamRegistry &, ServiceRegistry &)>;
       using ProcessCallback = std::function<void(const std::vector<DataRef> &, ServiceRegistry&, DataAllocator&)>;
       using ErrorCallback = std::function<void(const std::vector<DataRef> &, const ConfigParamRegistry &, const std::exception &)>;
       std::vector<InputSpec> inputs;
       std::vector<OutputSpec> outputs;
       std::vector<ConfigParamSpec> configParams;
       std::vector<std::string> requiredServices;
       AlgorithmSpec algorithm;
    };

In the above both `InputSpec` and `OutputSpec` are like:


    struct InputSpec {              // OutputSpec as well
      o2::Headers::DataDescription description;
      o2::Headers::DataOrigin origin;
      o2::Headers::SubSpecificationType subSpec;
      enum Lifetime lifetime;
    };

where description, origin and subSpec match the O2 Data Model definition. For the moment we will consider this a one to one mapping with the `o2::Headers::DataHeader` ones. In principle one could think of a one-to-many relationship (e.g. give me all the clusters, regardless of their provenance) and the processing layer could automatically aggregate those in a unique view. This is also the semantic difference between `InputSpec` and `OutputSpec`: the former is to express data that matches a given query (which must be exact at the moment) the latter is to describe in all details and without any doubt the kind of the produced outputs.

The `lifetime` property:


    enum Lifetime {
      Timeframe,
      Condition,
      QA,
      Transient
    };

can be used to distinguish if the associated payload should be considered payload data, and therefore be processed only once, or alignment / conditions data, and therefore it would be considered valid until a new copy is made available to the device.

The `configPar``a``ms` vector would be used to specify which configuration options the data processing being described requires:


    struct ConfigParamSpec {
      std::string name;
      enum ParamType type;
      variant defaultValue;
    };

command line / configuration options would be automatically generated by it. These are available only at init stage, and can be used to configure services. They are not available to the actual `process` callback as all the critical parameters for data processing should be part of the data stream itself, eventually coming from CCDB / ParameterManager.

Similarly the `requiredServices` vector would define which services are required for the data processing. For example this could be used to declare the need for some data cache, a GPU context, a thread pool.

The `algorithm` property, of `AlgorithmSpec` is instead used to specify the actual computation. Notice that the same `DataProcessorSpec` can used different `AlgorithmSpec`. The rationale for this is that while inputs and outputs might be the same, you might want to compare different versions of your algorithm. The `AlgorithmSpec` resembles the following:


    struct AlgorithmSpec {
      using ProcessCallback = std::function<void(const std::vector<DataRef>, ServiceRegistry&, DataAllocator&)>;
      using InitCallback = std::function<ProcessCallback(const ConfigParamRegistry &, ServiceRegistry &)>;
      using ErrorCallback = std::function<void(const std::vector<DataRef>, ServiceRegistry &, std::exception &e)>;

      InitCallback onInit = nullptr;
      ProcessCallback onProcess = nullptr;
      ErrorCallback onError = nullptr;
    };

The `onProcess` function is to be used for stateless computations. It’s a free function and it’s up to the framework to make sure that all the required components are declared upfront. It takes as input:


- References to the data which was actually requested.
- The set of services it declared as required  the computation.
- An allocator which can allocate new payloads only for the types which have been declared as `outputs`

`onProcess` is useful whenever your computation is fully contained in your input. In several cases, however, a computation requires some ancillary state, which needs to be initialised only on (re-)start of the job. For example you might want to initialise the geometry of your detector. To do so, you can use the `onInit` callback and allocate the state and pass it to the returned `ProcessCallback` as captured arguments. E.g:


    AlgorithmSpec{
      InitiCallBack{[](const ConfigParamRegistry &, ServiceRegistry &){
          auto statefulGeo = std::make_unique<TGeo>();
          return [geo = std::move(statefulGeo)](const std::vector<DataRef>, ServiceRegistry&, DataAllocator&) {
            // do something with geo
          };
        }
      }
    }

A `DataRef` would look like:


    struct DataRef {
      const InputSpec *spec;
      const char *const header;
      const char *const payload;
    };

`header` and `payload` are the pointers to the data which matches the `spec` InputSpec. (FIXME: header and payload should probably be some sort of FairMQBuffer).

## Instantiating a computation

Once a computation is described via a `DataProcessorSpec`, it will be passed to a `DataProcessorManager`, which looks like:


    class DataProcessorManager {
    public:
      void registerProcessor(DataProcessorSpec &spec);
      void onData(FairMQParts &parts);
    };


When the `FairMQDevice::InitTask` method is invoked, the `DataProcessorManager` will be responsible for:


- Making sure all the required options were correctly specified
- Creating all the services it required
- If available, invoking the init callback, which eventually will configure the services
- Register an OnData callback waiting for data on all the relevant input channels

Once the above initialisation phase is done, it will move to the RUN state and will:


- Make sure all the data required for the computation is there. If not, take ownership of the relevant messages and wait for some more to arrive.
- Make sure that the required condition data is there (or use a cached version, if allowed). If not, request to the parameter manager the correct version of the condition data.
- Invoke the specified `process` method
- Garbage collect messages which should have been processed and were not (e.g. because of an unresponsive upstream device), potentially notifying the fact via some monitoring facility.

Notice that all the required boilerplate could be hidden by having a generic `DataProcessingDevice` which uses a factory mechanism to load the actual code to be executed (with a proper plugin manager, similar to what others do), or alternatively a “static” approach could be followed, just like it happens with the  `runFairMQDevice.h` where most of the logic is hidden from the user who only has to provide a callback which returns one (or more) properly filled `DataProcessorSpec`.


    #include "Framework/Utils/runDataProcessing.h"

    void defineDataProcessing(std::vector<DataProcessorSpec &> specs) {
      // Fill a DataProcessingSpec "spec"
      ...
      specs.push_back(spec);
    }

This latter approach actually allows specifying a set of `DataProcessorSpec` which we could call a `WorkflowSpec`


    using WorkflowSpec = std::vector<o2::framework::DataProcessorSpec>;

Depending on how `runDataProcessing.h` is implemented, the executable could do different things:


- Spawn one process per entry in the `WorkflowSpec`, making sure they connect together.
- Generate a DDS topology.
- Dump a graphviz description of the DataFlow.
# Error handling

When an error happens during processing of some data, the writer of the `process` function should simply throw an exception. By default the exception is caught by the `DataProcessorManager` and a message is printed (if `std::exeception` derived `what()` method is used, otherwise a generic message is given). Users can provide themselves an error handler by specifying via the `onError` callback specified in `DataProcessorSpec`.


# Expressing parallelism (WIP)

If we want to retain a message passing semantic and really treat shared memory as yet another transport, we need to be very careful in how to express parallelism on data, so that the “single ownership model” of message passing forces us to either duplicate streams that need to be accessed in parallel, or to serialise workers which need to access the same data. Solutions like reference counting shared memory would not be allowed in such a scenario and in any case would require extra caution and support to make sure that failures do not leave dangling reference around (e.g. when one of the parallel workers abruptly terminates). First of all let’s consider the fact that there are two level of parallelisms which can be achieved:

- Data flow parallelism: when data can be split in partitions according to some subdivision criteria (e.g. have one stream per TPC sector and have one worker for each).

Time flow parallelism: when parallelism can be achieved by having different workers handle different time intervals for the incoming data. (e.g. worker 0 processes even timeframes, worker 1 processes odd timeframes).
Data flow parallelism is simply expressed by tuning the data flow, adding explicitly the parallel data paths, using the appropriate `InputSpec` and `OutputSpec`. E.g.:



    DataProcessorSpec{
      "tpc_processor_1",
      {InputSpec{"TPC", "CLUSTERS", SubSpec(1)}},
      ...
    },
    DataProcessorSpec{
      "tpc_processor_2",
      InputSpec{"TPC", "CLUSTERS", SubSpec(2)},
      ...
    }

or alternatively the parallel workflows part could be generated programmatically:


    parallel(
      DataProcessorSpec{
        "tpc_processor",
        {InputSpec{"TPC", "CLUSTERS"}}
      },
      ParallelInput{"TPC", "CLUSTERS"},  // For each InputSpec matching the signature
      [](){return {SubSpec(1), SubSpec(2)}} // Replace it with two copies, where subspecification is SubSpec(1) and SubSpec(2) respectively.
    )


# Services

Services are utility classes which `DataProcessor`s can access to request out-of-bound, deployment dependent, functionalities. For example a service could be used to post metrics to the monitoring system or to get a GPU context. The former would be dependent on whether you are running on your laptop (where monitoring could simply mean print out metrics on the command line) or in a large cluster (where monitoring probably means to send metrics to an aggregator device which then pushes them to the backend.
Services are initialised by the driver code (i.e. the code included via runDataProcessing.h) and passed to the user code via a `ServiceRegistry`. You can retrieve the service by the type of its interface class. E.g. for monitoring you can do:


    #include "Framework/MetricsService.h"
    ... // In the DataProcessor lambda...
    auto service = serviceRegistry.get<MetricsService>();
    service.post("my/metric", 1);
    ...
# Demonstrator (WIP)

An initial demonstrator illustrating a possible implementation of the design can be found at:

https://github.com/AliceO2Group/AliceO2/pull/456


In particular you can look at how a simple workflow is declared at:

https://github.com/AliceO2Group/AliceO2/pull/456/files#diff-07f6a9bf8f23c5a1e0502c24655aa648

The demonstator also includes a simple GUI to help debugging problems:

![](https://user-images.githubusercontent.com/10544/29307499-75bb8550-81a2-11e7-9aa6-96b7613288b5.png)


The GUI provides the following facilities:

- Graph view with all the connections between DataProcessors
- One log window per DataProcessor, allowing filtering and triggering on log messages
- Metrics inspector
# Features of the design
- **Explicit data flow:** Input and outputs are declared upfront and can be used for documentation or for automatic topology creation (assuming the actual processing environment is known).
- **Transport agnostic data processing:** users will not have to know about the details of how the data materialises on their device, they will always be handed the full set of payloads they requested, even if they come at different time.
- **Composability of data processing:** different process functions can in principle be chained and scheduled together depending on the desired granularity for devices.
# Implementation details (WIP)
## Mapping multiple DataFlows on the same set of devices

Linear flows of data are easy to handle. We just need to verify that all the inputs are satisfied by the output of some other device and verify that the beginning is a source (i.e. no inputs) while the end of the flow is a sink (no outputs). Things get more complicated when we have multiple devices whose outputs or a subset of them contributes to the input of one (e.g. when something needs to use the clusters produced by the TPC and those produced by ITS) or when outputs need to be reused by multiple devices. To handle those cases, we will need to allow output on different channels and introduce some sort of deduplication mechanism. If we want to support generic message passing, this kind of mechanism will have to either copy or serialise access, while reference counting is not an option (and complicates garbage collection). The copy would be ok for small messages, while the serialisation would be ok for less expensive algorithms especially if other stuff can be done in parallel. In case the two mentioned conditions are not matched and we end up to serialise something which ends up on the critical path, we need to rework the data flow to make it data parallel (e.g., split the TPC clusters by sector explicitly).

## DataAllocator API

In order to prevent algorithms to create data they do are not supposed to create, a special `DataAllocator` object is passed to the process callback, so that only messages for declared outputs can be created. The `DataAllocator` provides methods to create and adopt data in a number of formats. Currently supported formats are:

- Vanilla `char *` buffers with associated size
- TClonesArray (which gets serialised to a TMessage for exchange)
- PoD collection, as defined in `Framework/Collection.h` . The available API is the following:


    class DataAllocator {
    public:
      ...
      DataChunk newChunk(const OutputSpec &, size_t);
      DataChunk adoptChunk(const OutputSpec &, char *, size_t, fairmq_free_fn*, void *);
      TClonesArray &newTClonesArray(const OutputSpec &, const char *, size_t);
      template <class T>  Collection<T> newCollectionChunk(const OutputSpec &spec, size_t nElements);
    };

The DataChunk object resembles a `iovec`:

    struct DataChunk {
      char *data;
      size_t size;
    };

however, no API is provided to explicitly send it. All the created DataChunks are sent (potentially using scatter / gather) when the `process` function returns. This is to avoid the “modified after send” issues where a message which was sent is still owned and modifiable by the creator.

[ ] Describe the Collection based API for `DataAllocator`
## General notes
- While many of the features of this design resemble [ReactiveX](http://reactivex.io) and many of the details could be mapped to its jargon (event, observable, etc), we want to avoid naming choices which are either not familiar or confusing to physicists (which have a completely different understanding of “Event” or “Observable”).
# Interesting reads
- [MillWheel: Fault-Tolerant Stream Processing at Internet Scale](https://research.google.com/pubs/pub41378.html) : paper about Google last generation system for stream processing
- [Concord](http://concord.io) : Similar (to the above) stream processing solution, OpenSource
# General remarks & Feedback so far:
~~~~- Gvozden and Mikolaj were suggesting to have a multiple payload view-like object. Where does that fit? Shouldn’t this feature be provided by the DataRef binder?
- Do we need `process` to return a `bool` / an `error` code?
- What are the possible Services we can think about?
  - CPUTimer
  - File Reader
  - File Writer
- Thorsten: how do we express data parallelism?
- Thorsten: should configuration come always from the ccdb?
- Thorsten: should allow to specify that certain things are processed with the same CCDB conditions.
- Should we have different specifications for conditions objects and for data?
  - We could simplify current design by making Timeframe the default.
  - It complicates a bit the `process` method. We would need an extra allocator for the output objects and we would need an extra vector for the inputs.
  - On the other hand it would probably make some of the flow code easier to read / more type safe.
- Can inputs / outputs be optional? Most likely, no, since that would mean that if they arrive late the processing happens with a different set of inputs (and consequently a optional output means someone has an optional input). Do we need some “guaranteed delivery” for messages?
- ~~Do we need to guarantee that timeframes are processed in their natural order?~~ nope. Actually in general we cannot guarantee that.
- Do we want to separate “Algorithms”s  from “DataProcessor”s? The former would declare generic argument bindings (e.g. I take x, and y) and the latter would do the actual binding to real data (input clusters is y, input tracks is y). This is what tensor flow actually does.
- Shouldn’t the DataHeader contain the timeframe ID?
- Comment from David / Ruben: sometimes the input data depends on whether or not a detector is active during data taking. We would therefore need a mechanism to mask out inputs (and maybe modules) from the dataflow, based on run control. If only part of the data is available, it might make sense that we offer “fallback” callbacks which can work only on part of the data.
- Comment from Ruben: most likely people will want to also query the CCDB directly. Does it make sense to offer CCDB querying as a service so that we can intercept (and eventually optimise) multiple queries from the same workflow?
- Are options scoped? I.e. do we want to have that if two devices, `deviceA` and `deviceB` defining the same option (e.g. `mcEngine`) they will require / support using `--``deviceA-mcEngine` and `--``deviceB-mcEngine`?
- Mikolaj pointed out that capture by move is possible in `C++14` lambdas, so we should use that for the stateful init.
