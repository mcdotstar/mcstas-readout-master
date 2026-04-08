#include <string>

#include "Readout.h"
#include "reader.h"
#include "writer.h"

#ifdef __cplusplus
extern "C" {
#endif

  void readout_merge_files(const char * out_filename, const char ** in_filenames, const size_t count){
    if (count < 1){
      throw std::runtime_error("No input files provided");
    }
    DetectorType detector;
    ReadoutType readout;
    for (size_t i=0; i<count; i++){
      // Opening the file verifies that it is a valid readout file
      Reader reader(in_filenames[i]);
      if (0 == i){
        detector = reader.detector_type();
        readout = reader.readout_type();
      } else {
        if (detector != reader.detector_type() || readout != reader.readout_type()){
          throw std::runtime_error("Mismatched detector or readout types");
        }
      }
    }
    std::string dataset_name{"events"};
    Writer writer(out_filename, detector, readout, dataset_name);

    // this is certainly not the most efficient way to do this, but other methods would require
    // checking whether, e.g., the readouts will all fit into memory (or setting a chunk size), etc.
    for (size_t i=0; i<count; i++){
      Reader reader(in_filenames[i]);
      for (size_t j=0; j<reader.size(); j++){
        switch (readout){
          case ReadoutType::CAEN: writer.saveReadout(reader.get_CAEN(j, 1).front()); break;
          case ReadoutType::TTLMonitor: writer.saveReadout(reader.get_TTLMonitor(j, 1).front()); break;
          case ReadoutType::VMM3: writer.saveReadout(reader.get_VMM3(j, 1).front()); break;
          case ReadoutType::CDT: writer.saveReadout(reader.get_DREAM(j, 1).front()); break;
          default: throw std::runtime_error("Readout type not implemented");
        }
      }
    }

  }

//
#ifdef __cplusplus
}
#endif
