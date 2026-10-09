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
      .def_ro("logical_operation_count", &ResourceEstimate::logical_operation_count)
      .def_ro("stage", &ResourceEstimate::stage)
      .def_ro("depth_kind", &ResourceEstimate::depth_kind)
      .def_ro("target_verified", &ResourceEstimate::target_verified)
      .def_ro("t_count",         &ResourceEstimate::t_count);

  nb::class_<CompiledProgram>(m, "CompiledProgram")
      .def(nb::init<>())
      .def_prop_ro("ok",    &CompiledProgram::ok)
      .def_prop_ro("error", &CompiledProgram::error)
      .def_prop_ro("stage", &CompiledProgram::stage)
      .def_prop_ro("target_verified", &CompiledProgram::targetVerified)
      .def("dump_phonon",   &CompiledProgram::dumpPhonon)
      .def("dump_spinor",   &CompiledProgram::dumpSpinor)
      .def("estimate",      &CompiledProgram::estimate);
  m.attr("LogicalProgram") = m.attr("CompiledProgram");

  m.def("compile_phonon",
        [](std::string_view text, std::string_view target) {
          return CompiledProgram::fromPhononText(text, target);
        },
        nb::arg("text"), nb::arg("target") = "generic",
        "Compile a Phonon source to logical Spinor; target compatibility is not checked.");
  m.attr("compile_logical") = m.attr("compile_phonon");
}
