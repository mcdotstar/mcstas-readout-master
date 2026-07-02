# 2026-07-02 Status

This project is in a redevelopment state.
It is the interface between the simulation world (McStas based) and the ESS data acquisition world (EPICS, Kafka and the EFU).
As detailed in AGENT.md and architecture.md, the original design was based on a single McStas component, Readout, that would convert weighted rays to events and send them to an EFU during the simulation runtime.
The new design is based on a Collector class that collects weighted rays and stores them in an HDF5 file for later processing, which allows for more flexibility and better statistical correctness.

## Requirements:
1. A McStas component that collects weighted rays and sends them to the Collector class.
2. A Collector class that manages the collection of weighted rays and stores them in an HDF5 file.
3. A Reader class that can read the HDF5 file and provide an interface for accessing the collected data.
4. A replay mechanism that can read the collected data and send it to an EFU or other processing pipeline.
5. A set of command line utilities to combine multiple Collector files into a single file for replaying/reprocessing.
   1. the combined files might be from one repeated simulation point
   2. the combined files might be from multiple simulation points
6. A set of tests to verify the functionality of the Collector, Reader, and replay mechanisms.


## Outstanding issues:
### Switch to CollectorStar
Use the CollectorStar class to allow users to define their own data structures for collection, rather than being limited to the predefined structure in Collector.

This will allow for more flexibility in the types of data that can be collected and stored, and will enable users to collect additional information about the rays, such as their full state.
It will also reduce the maintenance overhead associated with the predefined structure in Collector, as users will be able to define their own structures as needed.
The CollectorStar class will need the same type of command line utilities and tests as the Collector class, to ensure that it can be used effectively in the same way.

### Replay and EFU integration
The replay mechanism should be able to read the collected data from the HDF5 file and send it to an EFU or other processing pipeline, in a way that is compatible with the original design
of the Readout class. This will allow for a seamless transition from the original design to the new design, and will enable users to continue using their existing EFU configurations without modification.

An important consideration is that one collector file can contain data intended for multiple EFUs, and the replay mechanism should be able to handle this by sending the appropriate data to each EFU based on the configuration of the simulation.
This means that it should be possible to provide the EFU configuration information to the Collector class directly.
