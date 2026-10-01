// Seemingly non-sensical header guard needed for McStas inclusion
#ifndef MCCODE_LIB_READOUT_H
#include "lib-readout.h"
#endif

void lib_readout_error(const char * comp_type, const char* named, const char * message, const char* variable){
  printf("%s(%s): %s %s, exiting.\n", comp_type, named, message, variable);
  exit(-1);
}

void readout_caen_error(const char * named, const char * variable){
  lib_readout_error("ReadoutCAEN", named, "Unknown particle variable", variable);
}

void collector_error(const char * named, const char * variable){
  lib_readout_error("CollectorSink", named, "Unknown particle variable", variable);
}

void collector_chopper_error(const char * named, const char * message){
  lib_readout_error("Collector*Chopper", named, message, "");
}


void readout_particle_check(const char * comp_type, const char * comp_name, _class_particle * p, const int present, char * name) {
  int failure=0;
  if (present){
    particle_getvar(p, name, &failure);
    if (failure) lib_readout_error(comp_type, comp_name, "No particle variable named", name);
  }
}


int readout_particle_getvar_int(_class_particle* p, char * name) {
  void * vval = particle_getvar_void(p, name, 0);
  return *(int*)vval;
}

void collector_sink_parameters(char * named) {
  for (int i=0; i<numipar; ++i){
    switch (mcinputtable[i].type){
      case instr_type_int:
        collector_sink_int(mcinputtable[i].name, *(int *)(mcinputtable[i].par), mcinputtable[i].unit, "");
        break;
      case instr_type_double:
        collector_sink_double(mcinputtable[i].name, *(double *)(mcinputtable[i].par), mcinputtable[i].unit, "");
        break;
      case instr_type_string:
        collector_sink_string(mcinputtable[i].name, *(char **)(mcinputtable[i].par), mcinputtable[i].unit, "");
        break;
      default:
        fprintf(stderr, "Warning(%s): Instrument parameter %s has unsupported type, not sending to CollectorSink.\n", named, mcinputtable[i].name);
    }
  }
}

/* ===========================================================================
 * Record sinks (see lib-readout.h)
 * ========================================================================= */

static int readout_strieq(const char * a, const char * b) {
  /* portable case-insensitive equality (MSVC has no POSIX strcasecmp) */
  for (; *a && *b; ++a, ++b) {
    char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a - 'A' + 'a') : *a;
    char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b - 'A' + 'a') : *b;
    if (ca != cb) return 0;
  }
  return *a == *b;
}

static char * readout_strdup(const char * s) {
  char * d;
  if (s == NULL) return NULL;
  d = (char *) malloc(strlen(s) + 1);
  if (d) strcpy(d, s);
  return d;
}

int readout_sink_kind(const char * sink, char * component) {
  int have_nexus = 0;
#ifdef USE_NEXUS
  have_nexus = (mcformat != NULL && strcasestr(mcformat, "NeXus") != NULL);
#endif
  if (sink == NULL || sink[0] == '\0' || readout_strieq(sink, "auto"))
    return have_nexus ? READOUT_SINK_NEXUS : READOUT_SINK_HDF5;
  if (readout_strieq(sink, "nexus")) {
    if (!have_nexus)
      lib_readout_error("ReadoutSink", component,
        "sink=\"nexus\" needs a binary built with NeXus support and run with --format=NeXus;", "use sink=\"hdf5\" or \"auto\"");
    return READOUT_SINK_NEXUS;
  }
  if (readout_strieq(sink, "hdf5")) return READOUT_SINK_HDF5;
  lib_readout_error("ReadoutSink", component, "Unknown sink (use auto, nexus or hdf5):", sink);
  return READOUT_SINK_HDF5;
}

readout_sink_t * readout_sink_new(const char * sink, const char * filename, const char * group,
                                  const char * description, int ess_type, uint64_t normalization,
                                  size_t record_size, const char * efu_address, int efu_port,
                                  char * component, int index, Coords position, Rotation rotation,
                                  int verbose) {
  readout_sink_t * s = (readout_sink_t *) calloc(1, sizeof(readout_sink_t));
  int i;
  if (s == NULL) lib_readout_error("ReadoutSink", component, "Out of memory", "");

  s->kind = readout_sink_kind(sink, component);
  s->description = readout_strdup(description);
  s->record_size = record_size;
  s->ess_type = ess_type;
  s->normalization = normalization;
  s->index = index;
  s->position = position;
  rot_copy(s->rotation, rotation);
  s->verbose = verbose;
  s->efu_address = (efu_address != NULL && efu_address[0] != '\0') ? readout_strdup(efu_address) : NULL;
  s->efu_port = efu_port;
  snprintf(s->component, sizeof(s->component), "%s", component);
  snprintf(s->group, sizeof(s->group), "%s", (group != NULL && group[0] != '\0') ? group : component);
  snprintf(s->nexuscomp, sizeof(s->nexuscomp), "%04d_%s", index - 1, component); /* as the McCode runtime names it */

  s->nfields = readout_description_fields(description, s->fields, READOUT_SINK_MAX_FIELDS);
  if (s->nfields < 0)
    lib_readout_error("ReadoutSink", component, "Could not parse the record description", description);
  if (s->nfields > READOUT_SINK_MAX_FIELDS)
    lib_readout_error("ReadoutSink", component, "Too many fields in the record description", description);
  if (readout_description_size(description) != record_size)
    lib_readout_error("ReadoutSink", component, "Record layout mismatch between component struct and description", description);
  s->weight_field = -1;
  for (i = 0; i < s->nfields; ++i)
    if (!strcmp(s->fields[i].name, "weight") && !strcmp(s->fields[i].type, "double") && s->fields[i].count == 1)
      s->weight_field = i;

#ifdef USE_MPI
  if (mpi_node_rank == mpi_node_root) {
#endif
    if (s->kind == READOUT_SINK_HDF5) {
      s->collector = collector_star_new(filename, s->group, description, ess_type, normalization);
      if (s->collector == NULL)
        lib_readout_error("ReadoutSink", component, "Could not create the Collector for file", filename);
      if (collector_record_size(s->collector) != record_size)
        lib_readout_error("ReadoutSink", component, "Record layout mismatch between component struct and description", description);
      if (s->efu_address != NULL && efu_port > 0)
        collector_efu(s->collector, s->efu_address, efu_port);
      /* several collectors may share one file: the first one records the instrument parameters */
      if (collector_sink_users(filename) == 1) collector_sink_parameters(component);
    }
    if (verbose > 1)
      printf("%s: storing %s records in %s%s%s\n", component, readout_detector_name(ess_type),
        s->kind == READOUT_SINK_NEXUS ? "the NeXus output file, group " : filename,
        s->kind == READOUT_SINK_NEXUS ? s->nexuscomp : "",
        s->kind == READOUT_SINK_NEXUS ? "" : "");
#ifdef USE_MPI
  }
#endif
  return s;
}

#ifdef USE_NEXUS
static int readout_nexus_type(const char * type) {
  if (!strcmp(type, "uint8_t")  || !strcmp(type, "unsigned char"))  return NX_UINT8;
  if (!strcmp(type, "int8_t")   || !strcmp(type, "char") || !strcmp(type, "signed char")) return NX_INT8;
  if (!strcmp(type, "uint16_t") || !strcmp(type, "unsigned short")) return NX_UINT16;
  if (!strcmp(type, "int16_t")  || !strcmp(type, "short"))          return NX_INT16;
  if (!strcmp(type, "uint32_t") || !strcmp(type, "unsigned int"))   return NX_UINT32;
  if (!strcmp(type, "int32_t")  || !strcmp(type, "int"))            return NX_INT32;
  if (!strcmp(type, "uint64_t") || !strcmp(type, "unsigned long long") || !strcmp(type, "size_t")) return NX_UINT64;
  if (!strcmp(type, "int64_t")  || !strcmp(type, "long long"))      return NX_INT64;
  if (!strcmp(type, "unsigned long")) return sizeof(unsigned long) == 8 ? NX_UINT64 : NX_UINT32;
  if (!strcmp(type, "long"))          return sizeof(long) == 8 ? NX_INT64 : NX_INT32;
  if (!strcmp(type, "float"))  return NX_FLOAT32;
  if (!strcmp(type, "double")) return NX_FLOAT64;
  return -1;
}

static void readout_nexus_string_attr(NXhandle h, const char * name, const char * value) {
  if (value != NULL) NXputattr(h, (char *) name, (void *) value, (int) strlen(value), NX_CHAR);
}

/* Append n records (master node) to entryN/instrument/components/<nexuscomp>/<group>,
   one dataset per field, first dimension unlimited. */
static void readout_nexus_append(readout_sink_t * s, const uint8_t * records, size_t n) {
  int i;
  const int64_t chunk_rows = 16384;
  if (!nxhandle || mcdisable_output_files) return;
  if (NXopengroup(nxhandle, "instrument", "NXinstrument") != NX_OK) {
    fprintf(stderr, "Warning(%s): no NeXus instrument group, records not written\n", s->component);
    return;
  }
  if (NXopengroup(nxhandle, "components", "NXdata") == NX_OK) {
    if (NXopengroup(nxhandle, s->nexuscomp, "NXdata") == NX_OK) {
      NXMDisableErrorReporting(); /* the group exists after the first save */
      NXmakegroup(nxhandle, s->group, "NXcollection");
      NXMEnableErrorReporting();
      if (NXopengroup(nxhandle, s->group, "NXcollection") == NX_OK) {
        for (i = 0; i < s->nfields; ++i) {
          const readout_field_t * f = &s->fields[i];
          int nxtype = readout_nexus_type(f->type);
          int rank = f->count > 1 ? 2 : 1;
          int64_t dims[2]  = { NX_UNLIMITED, (int64_t) f->count };
          int64_t chunk[2] = { chunk_rows, (int64_t) f->count };
          int64_t start[2] = { 0, 0 };
          int64_t size[2]  = { (int64_t) n, (int64_t) f->count };
          int64_t current[2] = { 0, 0 };
          int current_rank = 0, current_type = 0;
          size_t fsize = f->element_size * f->count, r;
          if (nxtype < 0) {
            fprintf(stderr, "Warning(%s): field %s has type %s without NeXus equivalent, skipped\n", s->component, f->name, f->type);
            continue;
          }
          NXMDisableErrorReporting();
          if (NXcompmakedata64(nxhandle, f->name, nxtype, rank, dims, NX_COMPRESSION, chunk) == NX_OK) {
            NXMEnableErrorReporting();
            if (NXopendata(nxhandle, f->name) != NX_OK) continue;
            if (!strcmp(f->name, "time")) readout_nexus_string_attr(nxhandle, "units", "s");
            readout_nexus_string_attr(nxhandle, "type", f->type);
          } else {
            NXMEnableErrorReporting();
            if (NXopendata(nxhandle, f->name) != NX_OK) continue;
            NXgetinfo64(nxhandle, &current_rank, current, &current_type);
          }
          if (n > 0) {
            uint8_t * column = (uint8_t *) malloc(n * fsize);
            if (column == NULL) lib_readout_error("ReadoutSink", s->component, "Out of memory writing field", f->name);
            for (r = 0; r < n; ++r) memcpy(column + r * fsize, records + r * s->record_size + f->offset, fsize);
            start[0] = current[0];
            if (NXputslab64(nxhandle, column, start, size) != NX_OK)
              fprintf(stderr, "Warning(%s): could not write %zu values of field %s to NeXus\n", s->component, n, f->name);
            free(column);
          }
          NXclosedata(nxhandle);
        }
        /* group attributes, rewritten at every save */
        {
          int32_t ess = (int32_t) s->ess_type;
          uint64_t total = s->written + n;
          NXputattr(nxhandle, "ess_type", &ess, 1, NX_INT32);
          readout_nexus_string_attr(nxhandle, "detector", readout_detector_name(s->ess_type));
          readout_nexus_string_attr(nxhandle, "description", s->description);
          NXputattr(nxhandle, "normalization", &s->normalization, 1, NX_UINT64);
          NXputattr(nxhandle, "records", &total, 1, NX_UINT64);
          readout_nexus_string_attr(nxhandle, "weight_convention",
            "weight = p * normalization; weight / normalization is the rate of the record");
          readout_nexus_string_attr(nxhandle, "type", "Readouts");
          if (s->efu_address != NULL && s->efu_port > 0) {
            int32_t port = (int32_t) s->efu_port;
            readout_nexus_string_attr(nxhandle, "efu_address", s->efu_address);
            NXputattr(nxhandle, "efu_port", &port, 1, NX_INT32);
          }
        }
        NXclosegroup(nxhandle); /* group */
      } else
        fprintf(stderr, "Warning(%s): could not open NeXus group %s\n", s->component, s->group);
      NXclosegroup(nxhandle); /* nexuscomp */
    } else
      fprintf(stderr, "Warning(%s): no NeXus component group %s\n", s->component, s->nexuscomp);
    NXclosegroup(nxhandle); /* components */
  }
  NXclosegroup(nxhandle); /* instrument */
}
#endif /* USE_NEXUS */

void readout_sink_save(readout_sink_t * s, array_t * array) {
  size_t local_count = array_size(array), count = 0, i;
  const uint8_t * records = NULL;
  uint8_t * gathered = NULL;
  double Nsum = 0, psum = 0, p2sum = 0;

  /* local statistics for the 0D monitor: p = weight / normalization */
  if (s->weight_field >= 0 && s->normalization > 0) {
    const uint8_t * local = array_data(array);
    for (i = 0; i < local_count; ++i) {
      double w;
      memcpy(&w, local + i * s->record_size + s->fields[s->weight_field].offset, sizeof(double));
      w /= (double) s->normalization;
      Nsum += 1; psum += w; p2sum += w * w;
    }
  }

#ifdef USE_MPI
  {
    /* gather all records on the master node */
    MPI_Datatype record_type;
    int local_n = (int) local_count, total_n = 0, node;
    int * counts = NULL, * displacements = NULL;
    MPI_Type_contiguous((int) s->record_size, MPI_BYTE, &record_type);
    MPI_Type_commit(&record_type);
    if (mpi_node_rank == mpi_node_root) {
      counts = (int *) calloc(mpi_node_count, sizeof(int));
      displacements = (int *) calloc(mpi_node_count, sizeof(int));
    }
    MPI_Gather(&local_n, 1, MPI_INT, counts, 1, MPI_INT, mpi_node_root, MPI_COMM_WORLD);
    if (mpi_node_rank == mpi_node_root) {
      for (node = 0; node < mpi_node_count; ++node) {
        displacements[node] = total_n;
        total_n += counts[node];
      }
      gathered = (uint8_t *) malloc((total_n > 0 ? total_n : 1) * s->record_size);
      if (gathered == NULL) lib_readout_error("ReadoutSink", s->component, "Out of memory gathering records", "");
    }
    MPI_Gatherv((void *) array_data(array), local_n, record_type,
                gathered, counts, displacements, record_type, mpi_node_root, MPI_COMM_WORLD);
    MPI_Type_free(&record_type);
    free(counts);
    free(displacements);
    records = gathered;
    count = (size_t) total_n;
  }
#else
  records = array_data(array);
  count = local_count;
#endif

#ifdef USE_MPI
  if (mpi_node_rank == mpi_node_root) {
#endif
    if (s->kind == READOUT_SINK_HDF5) {
      for (i = 0; i < count; ++i) {
        const uint8_t * record = records + i * s->record_size;
        double w = 0;
        if (s->weight_field >= 0) memcpy(&w, record + s->fields[s->weight_field].offset, sizeof(double));
        collector_star_add(s->collector, w, (const void *) record);
      }
    }
#ifdef USE_NEXUS
    else readout_nexus_append(s, records, count);
#endif
    s->written += count;
    if (s->verbose > 1) printf("%s: stored %zu records (%llu in total)\n", s->component, count, (unsigned long long) s->written);
#ifdef USE_MPI
  }
#endif
  free(gathered);
  array_clear(array);

  /* 0D monitor: number of records, summed rate and its error (MPI-reduced by the runtime) */
  mcdetector_out_0D("Readout records", Nsum, psum, p2sum, s->component, s->position, s->rotation, s->index);
}

void readout_sink_free(readout_sink_t * s) {
  if (s == NULL) return;
  if (s->collector != NULL) collector_free(s->collector);
  free(s->description);
  free(s->efu_address);
  free(s);
}
