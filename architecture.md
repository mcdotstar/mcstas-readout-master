# Architecture Needs
This develoment branch contains a number of new ideas, some of which should be combined into more-refined variants.

## Original project purpose
Connect a running McStas runtime to a waiting Event Formation Unit by converting rate-weighted neutron rays to an integer number of events, then bundling and sending over a network connection those events to an EFU.

This approach was chosen to meet the need for testing of the data acquisition pipeline from the end of the analogue world, through the data brokerage and file writer to the end of the data reduction.

While this need is met by the original approach, it does have a number of inherent drawbacks:
1. it is difficult to deploy in non-isolated environments due to the reliance on a running EFU
2. parallelization across rays at one simulation point is trivial, but parallelization across points is next to impossible
3. the conversion from weighted ray to events may be statistically flawed due to the rays each carrying a portion of the total count *rate* that would be collected by a *histogram bin*, so their statistical correctness is only guaranteed in aggregate

## New additions

### Collector
In an attempt to overcome all of the drawbacks of the current approach,
a new class Collector was added to replace the Readout class.

Where Readout converts rays to events on a per-ray basis, Collector only *collects* the weighted ray data needed to construct events.
The data is written to HDF5 using a structured datatype 1-D array.
The total weight is calculated and stored as a dataset attribute.
Once a simulation point is finished, the total count rate can be used to select a statistically correct number of rays to convert to events and send to an EFU.

This approach can remove the McStas-runtime need for a running EFU, since the ray data can be read back and sent after the fact.
This makes per-point parallelism trivial.

To further support this new mode, Collector borrows from UpdateEPICS and stores all instrument-level parameters in the HDF file for later use.

For any simulation point, any number of simultaneous datasets can be collected by separate McStas component instances.
Each can represent a segment of a detector, or a logical bin of some histogram, and all write to the same HDF5 file.
After the point is done, the total weight recorded in a dataset is then the expected count rate for that detector or logical bin,
which can be converted to events by first finding the expected counts by specifing a counting time,
then sampling from a Poisson distribution with the same mean value.

One drawback to this idea is the maintenance overhead associated with 
the per-detector/EFU structured datatype used in the dataset(s).

### StreamSampler
While the total number of events to sample from the weighted ray data is simple enough to find, 
selecting the rays to become events is less straight forward.

One method to achieve this is implented in SteamSampler,
which is the C++ reimplementation of a single method from the Julia package, StreamSampling.jl.
Specifically, it uses the Weighted Reservoir Sampling With Replacements algorithm (WRSWR).

The WRSWR algorithm identifies a pre-defined number of samples from a stream of weighted entries in one pass.
Once the scan point is finished, a number of events can be determined and then sampled.
TODO: do the WRSWR entries need to be normalized by the total events?


### CollectStar
To try and address the inflexibility of the Collector class, an almost-equivalent alternative was added.
CollectStar does not attempt to understand the data that is collected per ray,
instead a user defines the data type that will be stored in the form of a `const std::string` (or `const char *` from C) 
representation of the `struct`. 
CollectStar can both calculate the size of each struct and dynamicaly build the HDF5 structured data type for use in its 1-D dataset.

At TRACE time the user code then stores data by passing a weight and a `void *` to their data.
In this way, a user could (theoretically) store the full state of their ray (though not at the moment, since this requires nested `struct` definitions, which CollecStar does not yet parse),
or something of arbitrary complexity, e.g.,

```c
struct {
  double time;
  uint32_t pixel;
};
```

or

```c
struct {
  double time;
  uint16_t adc_a;
  uint16_t adc_b;
  uint8_t fibre;
  uint8_t fen;
  uint8_t channel;
};
```

## Needed improvements

### File combinability
Files created with CollectStar need to be combinable, possibly in multiple stages.

Multiple MPI workers write multiple HDF files, Collect can merge these files but that was not ported to CollectStar.

Multiple scan points _could_ be written to a single file -- with different points writing into sequentially named groups,
e.g., `point00`, `point01`, point02`, ..., `point15`, etc.
This approach does not work seamlesly with point-parallelism since only a single writer can access a file at a time.
Instead, it should be possible to merge files across points -- both the case of each point being in the root group of its file, and the case of one or more points already under the `pointNN` group(s).
This might be best accomplished via Python's h5py module, otherwise via a standalone binary.

### Sampled replay
A utility is needed to read-back the Collector/CollectStar HDF5 files, perform the per-dataset stream sampling (for any points in the file) and send the resulting events to an EFU.
The dataset could be augmented with a (number of) attribute(s) that specify the EFU parameters needed to send the data.

