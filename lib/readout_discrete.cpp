#include "readout_discrete.h"

#include <utility>

#include "DiscreteAfter.h"
#include "DiscreteWhile.h"
#include "Array.h"
#include "IndexSampler.h"

struct discreter_object {
    void *obj;
};
// struct collector_object {
//     void *collector;
// };

// C interface delegates to C++ implementation
extern "C" {

discreter_object * dr_new_desc(const char * description, const char * name, const size_t samples, const uint32_t seed) {
    const auto obj = new discreter_object;
    obj->obj = new DiscreteWhile(description, name, samples, seed);
    return obj;
}

discreter_object * dr_new_size(const size_t object_bytes, const char * name, const size_t samples, const uint32_t seed) {
    const auto obj = new discreter_object;
    obj->obj = new DiscreteWhile(object_bytes, name, samples, seed);
    return obj;
}

discreter_object * dr_new(const char * description, const size_t object_bytes, const char * name, const size_t samples, const uint32_t seed) {
    const auto obj = new discreter_object;
    obj->obj = new DiscreteWhile(description, object_bytes, name, samples, seed);
    return obj;
}

void dr_free(discreter_object *obj) {
    if (obj == nullptr) return;
    if (obj->obj) {
        delete static_cast<DiscreteWhile*>(obj->obj);
    }
    free(obj);
}


void dr_fit(const discreter_object *obj, const void * src, const double weight) {
    if (obj == nullptr) return;
    if (obj->obj) {
        static_cast<DiscreteWhile*>(obj->obj)->fit(src, weight);
    }
}


int dr_filling(const discreter_object *obj) {
    if (obj == nullptr) return 0;
    if (obj->obj) {
        return static_cast<DiscreteWhile*>(obj->obj)->filling();
    }
    return 0;
}


size_t dr_value(const discreter_object *obj, void * buffer, const size_t buffer_size) {
    if (obj == nullptr) return 0;
    if (obj->obj) {
        const auto values = static_cast<DiscreteWhile*>(obj->obj)->value();
        const auto bytes_to_write = std::min(buffer_size, values.size() * static_cast<size_t>(static_cast<DiscreteWhile*>(obj->obj)->object_size()));
        std::memcpy(buffer, values.data(), bytes_to_write);
        return bytes_to_write;
    }
    return 0;
}

size_t dr_value_index(const discreter_object *obj, const size_t index, void * buffer, const size_t buffer_size) {
    if (obj == nullptr) return 0;
    if (obj->obj) {
        static_cast<DiscreteWhile*>(obj->obj)->value(index, buffer);
        return std::min(buffer_size, static_cast<size_t>(static_cast<DiscreteWhile*>(obj->obj)->object_size()));
    }
    return 0;
}
//
// /// CollectorStar interface
// ///
// /// Create a new CollectorStar object with the given description and dataset name
// collector_t* cs_new_desc(const char* type_description, const char* dataset_name) {
//     const auto obj = new collector_object;
//     obj->collector = new CollectorStar(type_description, dataset_name);
//     return obj;
// }
//
// collector_t* cs_new_size(const size_t object_size, const char* dataset_name) {
//     const auto obj = new collector_object;
//     obj->collector = new CollectorStar(object_size, dataset_name);
//     return obj;
// }
//
// collector_t* cs_new(const char* type_description, const size_t object_size, const char* dataset_name) {
//     const auto obj = new collector_object;
//     obj->collector = new CollectorStar(type_description, object_size, dataset_name);
//     return obj;
// }
//
// void cs_free(collector_t* cs) {
//     if (cs == nullptr) return;
//     delete static_cast<CollectorStar*>(cs->collector);
//     free(cs);
// }
//
// void cs_add(const collector_t* cs, const void* src) {
//     if (cs == nullptr) return;
//     static_cast<CollectorStar*>(cs->collector)->add(src);
// }
//
// int cs_get(const collector_t* cs, const size_t index, void* dst) {
//     if (cs == nullptr) return 0;
//     static_cast<CollectorStar*>(cs->collector)->get(index, dst);
//     return 1;
// }
//
// size_t cs_count(const collector_t* cs) {
//     if (cs == nullptr) return 0;
//     return static_cast<CollectorStar*>(cs->collector)->count();
// }
//
// size_t cs_object_size(const collector_t* cs) {
//     if (cs == nullptr) return 0;
//     return static_cast<CollectorStar*>(cs->collector)->object_size();
// }
//
// int cs_write_hdf5(const collector_t* cs, const char* filename) {
//     if (cs == nullptr) return 0;
//     static_cast<CollectorStar*>(cs->collector)->write_hdf5(filename);
//     return 1;
// }

struct array_object {
    void * obj;
};

array_t * new_array(size_t bytes_per_object) {
    const auto obj = new array_object;
    obj->obj = new Array(bytes_per_object);
    return obj;
}

void delete_array(array_t * array) {
    if (array == nullptr) return;
    delete static_cast<Array*>(array->obj);
    array->obj = nullptr;
}
size_t array_size(const array_t * array) {
    if (array == nullptr) return 0;
    return static_cast<Array*>(array->obj)->count();
}
void array_add(const array_t * array, const void * data) {
    if (array == nullptr) return;
    static_cast<Array*>(array->obj)->add(data);
}
void array_get(const array_t * array, const size_t index, void * dst) {
    if (array == nullptr) return;
    static_cast<Array*>(array->obj)->get(index, dst);
}

struct index_sampler_object {
    void * obj;
};

index_sampler_t * new_index_sampler(const size_t samples, const uint32_t seed) {
    const auto obj = new index_sampler_object;
    obj->obj = new IndexSampler(samples, seed);
    return obj;
}

void delete_index_sampler(index_sampler_t * sampler) {
    if (sampler == nullptr) return;
    delete static_cast<IndexSampler*>(sampler->obj);
    sampler->obj = nullptr;
}

void index_sampler_fit(const index_sampler_t * sampler, const size_t index, const double weight) {
    if (sampler == nullptr) return;
    static_cast<IndexSampler*>(sampler->obj)->fit(index, weight);
}

int index_sampler_filling(const index_sampler_t * sampler) {
    if (sampler == nullptr) return 0;
    return static_cast<IndexSampler*>(sampler->obj)->filling();
}
void index_sampler_values(const index_sampler_t * sampler, size_t * values) {
    if (sampler == nullptr) return;
    std::ranges::copy(static_cast<IndexSampler*>(sampler->obj)->value(), values);
}
size_t index_sampler_value(const index_sampler_t * sampler, const size_t index) {
    if (sampler == nullptr) return 0;
    return static_cast<IndexSampler*>(sampler->obj)->value(index);
}

} // extern "C"
