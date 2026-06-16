// photon/bindings/python/Module.cpp
//
// nanobind module: photon._engine
//
// Thin Python surface over photon::bindings::CompiledProgram.

#include "photon/bindings/Engine.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>

namespace nb = nanobind;

NB_MODULE(_engine, m) {
  using photon::bindings::CompiledProgram;
  using photon::bindings::ResourceEstimate;

  nb::class_<ResourceEstimate>(m, "ResourceEstimate")
      .def_ro("num_qubits",      &ResourceEstimate::num_qubits)
      .def_ro("depth",           &ResourceEstimate::depth)
      .def_ro("two_qubit_count", &ResourceEstimate::two_qubit_count)
      .def_ro("t_count",         &ResourceEstimate::t_count);

  nb::class_<CompiledProgram>(m, "CompiledProgram")
      .def(nb::init<>())
      .def_prop_ro("ok",    &CompiledProgram::ok)
      .def_prop_ro("error", &CompiledProgram::error)
      .def("dump_phonon",   &CompiledProgram::dumpPhonon)
      .def("dump_spinor",   &CompiledProgram::dumpSpinor)
      .def("estimate",      &CompiledProgram::estimate);

  m.def("compile_phonon",
        [](std::string_view text, std::string_view target) {
          return CompiledProgram::fromPhononText(text, target);
        },
        nb::arg("text"), nb::arg("target") = "generic",
        "Compile a Phonon-text source into a CompiledProgram.");
}
