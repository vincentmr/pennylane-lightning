// Copyright 2018-2023 Xanadu Quantum Technologies Inc.

// Licensed under the Apache License, Version 2.0 (the License);
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

// http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an AS IS BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <Kokkos_Core.hpp>
#include <Kokkos_Random.hpp>

#include "LinearAlgebraKokkos.hpp" // getRealOfComplexInnerProduct
#include "MeasurementsBase.hpp"
#include "MeasuresFunctors.hpp"
#include "Observables.hpp"
#include "ObservablesKokkos.hpp"
#include "Util.hpp"

/// @cond DEV
namespace {
using namespace Pennylane::LightningKokkos::Functors;
using namespace Pennylane::Measures;
using namespace Pennylane::Observables;
using Pennylane::LightningKokkos::Util::getRealOfComplexInnerProduct;
using Pennylane::LightningKokkos::Util::SparseMV_Kokkos;
} // namespace
/// @endcond

namespace Pennylane::LightningKokkos::Measures {
template <class StateVectorT>
class MeasurementsMPI final
    : public MeasurementsBase<StateVectorT, MeasurementsMPI<StateVectorT>> {
  private:
    using PrecisionT = typename StateVectorT::PrecisionT;
    using ComplexT = typename StateVectorT::ComplexT;
    using BaseType =
        MeasurementsBase<StateVectorT, MeasurementsMPI<StateVectorT>>;

    using KokkosExecSpace = typename StateVectorT::KokkosExecSpace;
    using KokkosVector = typename StateVectorT::KokkosVector;
    using KokkosSizeTVector = typename StateVectorT::KokkosSizeTVector;
    using UnmanagedSizeTHostView =
        typename StateVectorT::UnmanagedSizeTHostView;
    using UnmanagedConstComplexHostView =
        typename StateVectorT::UnmanagedConstComplexHostView;
    using UnmanagedConstSizeTHostView =
        typename StateVectorT::UnmanagedConstSizeTHostView;
    using UnmanagedPrecisionHostView =
        typename StateVectorT::UnmanagedPrecisionHostView;

  public:
    explicit MeasurementsMPI(const StateVectorT &statevector)
        : BaseType{statevector} {};

    /**
     * @brief Expectation value of an observable.
     *
     * @param sv Observable-state-vector product.
     * @return Floating point expectation value of the observable.
     */
    PrecisionT expval(const StateVectorT &sv) {
        const PrecisionT expected_value = getRealOfComplexInnerProduct(
            this->_statevector.getView(), sv.getView());
        return this->_statevector.all_reduce_sum(expected_value);
    }
    /**
     * @brief Expectation value of an observable.
     *
     * @param matrix Square matrix in row-major order.
     * @param wires Wires where to apply the operator.
     * @return Floating point expectation value of the observable.
     */
    PrecisionT expval(const std::vector<ComplexT> &matrix,
                      const std::vector<std::size_t> &wires) {
        StateVectorT ob_sv{this->_statevector};
        ob_sv.applyOperation("Matrix", wires, false, {}, matrix);
        return expval(ob_sv);
    };

    /**
     * @brief Expectation value of an observable.
     *
     * @param operation String with the operator name.
     * @param wires Wires where to apply the operator.
     * @return Floating point expectation value of the observable.
     */
    PrecisionT expval(const std::string &operation,
                      const std::vector<size_t> &wires) {
        StateVectorT ob_sv{this->_statevector};
        ob_sv.applyOperation(operation, wires);
        return expval(ob_sv);
    };

    /**
     * @brief Calculate expectation value for a general Observable.
     *
     * @param ob Observable.
     * @return Expectation value with respect to the given observable.
     */
    PrecisionT expval(const Observable<StateVectorT> &ob) {
        StateVectorT ob_sv{this->_statevector};
        ob.applyInPlace(ob_sv);
        const PrecisionT expected_value = getRealOfComplexInnerProduct(
            this->_statevector.getView(), ob_sv.getView());
        const PrecisionT result =
            this->_statevector.all_reduce_sum(expected_value);
        return result;
    }

    /**
     * @brief Expectation value for a list of observables.
     *
     * @tparam op_type Operation type.
     * @param operations_list List of operations to measure.
     * @param wires_list List of wires where to apply the operators.
     * @return Floating point std::vector with expectation values for the
     * observables.
     */
    template <typename op_type>
    std::vector<PrecisionT>
    expval(const std::vector<op_type> &operations_list,
           const std::vector<std::vector<std::size_t>> &wires_list) {
        PL_ABORT_IF(
            (operations_list.size() != wires_list.size()),
            "The lengths of the list of operations and wires do not match.");
        std::vector<PrecisionT> expected_value_list;

        for (std::size_t index = 0; index < operations_list.size(); index++) {
            expected_value_list.emplace_back(
                expval(operations_list[index], wires_list[index]));
        }

        return expected_value_list;
    }

    /**
     * @brief Variance of an observable.
     *
     * @param sv Observable-state-vector product.
     * @return Floating point variance of the observable.
     */
    PrecisionT var(const StateVectorT &sv) {
        const PrecisionT mean_square =
            getRealOfComplexInnerProduct(sv.getView(), sv.getView());
        const PrecisionT squared_mean = static_cast<PrecisionT>(
            std::pow(getRealOfComplexInnerProduct(this->_statevector.getView(),
                                                  sv.getView()),
                     2));
        const PrecisionT variance = mean_square - squared_mean;
        return this->_statevector.all_reduce_sum(variance);
    }

    /**
     * @brief Variance of an observable.
     *
     * @param matrix Square matrix in row-major order.
     * @param wires Wires where to apply the operator.
     * @return Floating point variance of the observable.
     */
    PrecisionT var(const std::vector<ComplexT> &matrix,
                   const std::vector<std::size_t> &wires) {
        StateVectorT ob_sv{this->_statevector};
        ob_sv.applyOperation("Matrix", wires, false, {}, matrix);
        return var(ob_sv);
    };

    /**
     * @brief Variance of an observable.
     *
     * @param operation String with the operator name.
     * @param wires Wires where to apply the operator.
     * @return Floating point variance of the observable.
     */
    PrecisionT var(const std::string &operation,
                   const std::vector<size_t> &wires) {
        StateVectorT ob_sv{this->_statevector};
        ob_sv.applyOperation(operation, wires);
        return var(ob_sv);
    };

    /**
     * @brief Calculate variance for a general Observable.
     *
     * @param ob Observable.
     * @return variance with respect to the given observable.
     */
    PrecisionT var(const Observable<StateVectorT> &ob) {
        StateVectorT ob_sv{this->_statevector};
        ob.applyInPlace(ob_sv);
        return var(ob_sv);
    }

    /**
     * @brief Variance for a list of observables.
     *
     * @tparam op_type Operation type.
     * @param operations_list List of operations to measure.
     * @param wires_list List of wires where to apply the operators.
     * @return Floating point std::vector with variances for the
     * observables.
     */
    template <typename op_type>
    std::vector<PrecisionT>
    var(const std::vector<op_type> &operations_list,
        const std::vector<std::vector<std::size_t>> &wires_list) {
        PL_ABORT_IF(
            (operations_list.size() != wires_list.size()),
            "The lengths of the list of operations and wires do not match.");
        std::vector<PrecisionT> variance_list;

        for (std::size_t index = 0; index < operations_list.size(); index++) {
            variance_list.emplace_back(
                var(operations_list[index], wires_list[index]));
        }

        return variance_list;
    }
};

} // namespace Pennylane::LightningKokkos::Measures