#include <Readout.h>
#include <stdio.h>

int main(void) {
  /* the C API is usable without HDF5/HighFive headers */
  if (collector_record_size(NULL) != 0) return 1;
  printf("consumer ok\n");
  return 0;
}
