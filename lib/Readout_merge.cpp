#include <string>

#include "Readout.h"
#include "reader.h"
#include "writer.h"

#ifdef __cplusplus
extern "C" {
#endif

// TODO replace this with the Collector implementation

  void readout_merge_files(const char * out_filename, const char ** in_filenames, const size_t count){
    if (count < 1){
      throw std::runtime_error("No input files provided");
    }
    DetectorType detector;
    ReadoutType readout;
    std::string dataset_name;
    for (size_t i=0; i<count; i++){
      ReaderSource source(in_filenames[i]);
      if (source.readers().size() != 1u) {
        throw std::runtime_error("readout_merge_files currently supports input files with exactly one collector group");
      }
      const auto & reader = source.readers().front();
      if (0 == i){
        detector = reader.detector_type();
        readout = reader.readout_type();
        dataset_name = reader.collector_name();
      } else {
        if (detector != reader.detector_type() || readout != reader.readout_type()){
          throw std::runtime_error("Mismatched detector or readout types");
        }
        if (dataset_name != reader.collector_name()) {
          throw std::runtime_error("Mismatched collector group names");
        }
      }
    }
    Writer writer(out_filename, detector, readout, dataset_name);

    // this is certainly not the most efficient way to do this, but other methods would require
    // checking whether, e.g., the readouts will all fit into memory (or setting a chunk size), etc.
    for (size_t i=0; i<count; i++){
      ReaderSource source(in_filenames[i]);
      const auto & reader = source.readers().front();
      for (size_t j=0; j<reader.size(); j++){
        switch (readout){
          case ReadoutType::CAEN: writer.saveReadout(reader.get_CAEN(j, 1).front()); break;
          case ReadoutType::TTLMonitor: writer.saveReadout(reader.get_TTLMonitor(j, 1).front()); break;
          case ReadoutType::VMM3: writer.saveReadout(reader.get_VMM3(j, 1).front()); break;
          case ReadoutType::CDT: writer.saveReadout(reader.get_CDT(j, 1).front()); break;
          default: throw std::runtime_error("Readout type not implemented");
        }
      }
    }

  }

//
#ifdef __cplusplus
}
#endif
