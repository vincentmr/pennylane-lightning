// Copyright 2022-2023 Xanadu Quantum Technologies Inc. and contributors.

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <functional>
#include <vector>

#include "CSRMatrix.hpp"
#include "Constant.hpp"
#include "ConstantUtil.hpp" // lookup
#include "LinearAlg.hpp"
#include "Observables.hpp"
#include "StateVectorCudaMPI.hpp"
#include "StateVectorCudaManaged.hpp"
#include "Util.hpp"
#include "cuError.hpp"

/// @cond DEV
namespace {
using namespace Pennylane::Util;
using namespace Pennylane::Observables;
using namespace Pennylane::LightningGPU::Util;
using namespace Pennylane::LightningGPU::MPI;
using Pennylane::LightningGPU::StateVectorCudaManaged;
using Pennylane::LightningGPU::StateVectorCudaMPI;
} // namespace
/// @endcond

namespace Pennylane::LightningGPU::Observables {

/**
 * @brief Class models named observables (PauliX, PauliY, PauliZ, etc.)
 *
 * @tparam StateVectorT State vector class.
 */
template <typename StateVectorT>
class NamedObsMPI final : public NamedObsBase<StateVectorT> {
  private:
    using BaseType = NamedObsBase<StateVectorT>;

  public:
    using PrecisionT = typename StateVectorT::PrecisionT;
    /**
     * @brief Construct a NamedObs object, representing a given observable.
     *
     * @param obs_name Name of the observable.
     * @param wires Argument to construct wires.
     * @param params Argument to construct parameters
     */
    NamedObsMPI(std::string obs_name, std::vector<size_t> wires,
                std::vector<PrecisionT> params = {})
        : BaseType{obs_name, wires, params} {
        using Pennylane::Gates::Constant::gate_names;
        using Pennylane::Gates::Constant::gate_num_params;
        using Pennylane::Gates::Constant::gate_wires;

        const auto gate_op = lookup(reverse_pairs(gate_names),
                                    std::string_view{this->obs_name_});
        PL_ASSERT(lookup(gate_wires, gate_op) == this->wires_.size());
        PL_ASSERT(lookup(gate_num_params, gate_op) == this->params_.size());
    }
};

/**
 * @brief Class models arbitrary Hermitian observables
 *
 */

template <class StateVectorT>
class HermitianObsMPI final : public HermitianObsBase<StateVectorT> {
  private:
    using BaseType = HermitianObsBase<StateVectorT>;

  public:
    using PrecisionT = typename StateVectorT::PrecisionT;
    using MatrixT = std::vector<std::complex<PrecisionT>>;
    using ComplexT = typename StateVectorT::ComplexT;

    /**
     * @brief Create Hermitian observable
     *
     * @param matrix Matrix in row major format.
     * @param wires Wires the observable applies to.
     */
    HermitianObsMPI(MatrixT matrix, std::vector<size_t> wires)
        : BaseType{matrix, wires} {}
};

/**
 * @brief Class models Tensor product observables
 */
template <class StateVectorT>
class TensorProdObsMPI final : public TensorProdObsBase<StateVectorT> {
  private:
    using BaseType = TensorProdObsBase<StateVectorT>;

  public:
    using PrecisionT = typename StateVectorT::PrecisionT;
    /**
     * @brief Create a tensor product of observables
     *
     * @param arg Arguments perfect forwarded to vector of observables.
     */
    template <typename... Ts>
    explicit TensorProdObsMPI(Ts &&...arg) : BaseType{arg...} {}

    /**
     * @brief Convenient wrapper for the constructor as the constructor does not
     * convert the std::shared_ptr with a derived class correctly.
     *
     * This function is useful as std::make_shared does not handle
     * brace-enclosed initializer list correctly.
     *
     * @param obs List of observables
     */
    static auto
    create(std::initializer_list<std::shared_ptr<Observable<StateVectorT>>> obs)
        -> std::shared_ptr<TensorProdObsMPI<StateVectorT>> {
        return std::shared_ptr<TensorProdObsMPI<StateVectorT>>{
            new TensorProdObsMPI(std::move(obs))};
    }

    static auto
    create(std::vector<std::shared_ptr<Observable<StateVectorT>>> obs)
        -> std::shared_ptr<TensorProdObsMPI<StateVectorT>> {
        return std::shared_ptr<TensorProdObsMPI<StateVectorT>>{
            new TensorProdObsMPI(std::move(obs))};
    }
};

/**
 * @brief General Hamiltonian as a sum of observables.
 *
 */
template <class StateVectorT>
class HamiltonianMPI final : public HamiltonianBase<StateVectorT> {
  private:
    using BaseType = HamiltonianBase<StateVectorT>;

  public:
    using PrecisionT = typename StateVectorT::PrecisionT;
    using ComplexT = typename StateVectorT::ComplexT;

    /**
     * @brief Create a Hamiltonian from coefficients and observables
     *
     * @param coeffs Arguments to construct coefficients
     * @param obs Arguments to construct observables
     */
    template <typename T1, typename T2>
    explicit HamiltonianMPI(T1 &&coeffs, T2 &&obs) : BaseType{coeffs, obs} {}

    /**
     * @brief Convenient wrapper for the constructor as the constructor does not
     * convert the std::shared_ptr with a derived class correctly.
     *
     * This function is useful as std::make_shared does not handle
     * brace-enclosed initializer list correctly.
     *
     * @param coeffs Argument to construct coefficients
     * @param obs Argument to construct terms
     */
    static auto
    create(std::initializer_list<PrecisionT> coeffs,
           std::initializer_list<std::shared_ptr<Observable<StateVectorT>>> obs)
        -> std::shared_ptr<HamiltonianMPI<StateVectorT>> {
        return std::shared_ptr<HamiltonianMPI<StateVectorT>>(
            new HamiltonianMPI<StateVectorT>{std::move(coeffs),
                                             std::move(obs)});
    }

    // to work with
    void applyInPlace(StateVectorT &sv) const override {
        using CFP_t = typename StateVectorT::CFP_t;
        DataBuffer<CFP_t, int> buffer(sv.getDataBuffer().getLength(),
                                      sv.getDataBuffer().getDevTag());
        buffer.zeroInit();

        for (size_t term_idx = 0; term_idx < this->coeffs_.size(); term_idx++) {
            DevTag<int> dt_local(sv.getDataBuffer().getDevTag());
            dt_local.refresh();
            StateVectorT tmp(dt_local, sv.getNumGlobalQubits(),
                             sv.getNumLocalQubits(), sv.getData());
            this->obs_[term_idx]->applyInPlace(tmp);
            scaleAndAddC_CUDA(
                std::complex<PrecisionT>{this->coeffs_[term_idx], 0.0},
                tmp.getData(), buffer.getData(), tmp.getLength(),
                tmp.getDataBuffer().getDevTag().getDeviceID(),
                tmp.getDataBuffer().getDevTag().getStreamID(),
                tmp.getCublasCaller());
        }

        sv.CopyGpuDataToGpuIn(buffer.getData(), buffer.getLength());
    }
};

/**
 * @brief Sparse representation of Hamiltonian<StateVectorT>
 *
 */
template <class StateVectorT>
class SparseHamiltonianMPI final : public SparseHamiltonianBase<StateVectorT> {
  public:
    using PrecisionT = typename StateVectorT::PrecisionT;
    using ComplexT = typename StateVectorT::ComplexT;
    // cuSparse required index type
    using IdxT =
        typename std::conditional<std::is_same<PrecisionT, float>::value,
                                  int32_t, int64_t>::type;

  private:
    using BaseType = SparseHamiltonianBase<StateVectorT>;

  public:
    /**
     * @brief Create a SparseHamiltonianMPI from data, indices and offsets in
     * CSR format.
     *
     * @param data Arguments to construct data
     * @param indices Arguments to construct indices
     * @param offsets Arguments to construct offsets
     * @param wires Arguments to construct wires
     */
    template <typename T1, typename T2, typename T3 = T2, typename T4>
    explicit SparseHamiltonianMPI(T1 &&data, T2 &&indices, T3 &&offsets,
                                  T4 &&wires)
        : BaseType{data, indices, offsets, wires} {}

    /**
     * @brief Convenient wrapper for the constructor as the constructor does not
     * convert the std::shared_ptr with a derived class correctly.
     *
     * This function is useful as std::make_shared does not handle
     * brace-enclosed initializer list correctly.
     *
     * @param data Argument to construct data
     * @param indices Argument to construct indices
     * @param offsets Argument to construct ofsets
     * @param wires Argument to construct wires
     */
    static auto create(std::initializer_list<ComplexT> data,
                       std::initializer_list<IdxT> indices,
                       std::initializer_list<IdxT> offsets,
                       std::initializer_list<std::size_t> wires)
        -> std::shared_ptr<SparseHamiltonianMPI<StateVectorT>> {
        return std::shared_ptr<SparseHamiltonianMPI<StateVectorT>>(
            new SparseHamiltonianMPI<StateVectorT>{
                std::move(data), std::move(indices), std::move(offsets),
                std::move(wires)});
    }

    /**
     * @brief Updates the statevector SV:->SV', where SV' = a*H*SV, and where H
     * is a sparse Hamiltonian.
     *
     */
    void applyInPlace(StateVectorT &sv) const override {
        auto mpi_manager = sv.getMPIManager();
        if (mpi_manager.getRank() == 0) {
            PL_ABORT_IF_NOT(
                this->wires_.size() == sv.getTotalNumQubits(),
                "SparseH wire count does not match state-vector size");
        }
        using CFP_t = typename StateVectorT::CFP_t;

        // Distribute sparse matrix across multi-nodes/multi-gpus
        size_t num_rows = size_t{1} << sv.getTotalNumQubits();
        size_t local_num_rows = size_t{1} << sv.getNumLocalQubits();

        std::vector<std::vector<CSRMatrix<PrecisionT, IdxT>>> csrmatrix_blocks;

        if (mpi_manager.getRank() == 0) {
            csrmatrix_blocks = splitCSRMatrix<PrecisionT, IdxT>(
                mpi_manager, num_rows, this->offsets_.data(),
                this->indices_.data(), this->data_.data());
        }
        mpi_manager.Barrier();

        std::vector<CSRMatrix<PrecisionT, IdxT>> localCSRMatVector;
        for (size_t i = 0; i < mpi_manager.getSize(); i++) {
            auto localCSRMat = scatterCSRMatrix<PrecisionT, IdxT>(
                mpi_manager, csrmatrix_blocks[i], local_num_rows, 0);
            localCSRMatVector.push_back(localCSRMat);
        }

        mpi_manager.Barrier();

        auto device_id = sv.getDataBuffer().getDevTag().getDeviceID();
        auto stream_id = sv.getDataBuffer().getDevTag().getStreamID();
        cusparseHandle_t handle = sv.getCusparseHandle();

        const size_t length_local = size_t{1} << sv.getNumLocalQubits();

        std::unique_ptr<DataBuffer<CFP_t>> d_sv_prime =
            std::make_unique<DataBuffer<CFP_t>>(length_local, device_id,
                                                stream_id, true);
        std::unique_ptr<DataBuffer<CFP_t>> d_tmp =
            std::make_unique<DataBuffer<CFP_t>>(length_local, device_id,
                                                stream_id, true);
        d_sv_prime->zeroInit();
        PL_CUDA_IS_SUCCESS(cudaDeviceSynchronize());
        mpi_manager.Barrier();

        for (size_t i = 0; i < mpi_manager.getSize(); i++) {
            size_t color = 0;
            auto &localCSRMatrix = localCSRMatVector[i];

            if (localCSRMatrix.getValues().size() != 0) {
                color = 1;
                SparseMV_cuSparse<IdxT, PrecisionT, CFP_t>(
                    localCSRMatrix.getCsrOffsets().data(),
                    localCSRMatrix.getCsrOffsets().size(),
                    localCSRMatrix.getColumns().data(),
                    localCSRMatrix.getValues().data(),
                    localCSRMatrix.getValues().size(), sv.getData(),
                    d_sv_prime->getData(), device_id, stream_id, handle);
            }
            PL_CUDA_IS_SUCCESS(cudaDeviceSynchronize());
            mpi_manager.Barrier();

            if (mpi_manager.getRank() == i) {
                color = 1;
                if (localCSRMatrix.getValues().size() == 0) {
                    d_tmp->zeroInit();
                }
            }
            PL_CUDA_IS_SUCCESS(cudaDeviceSynchronize());
            mpi_manager.Barrier();

            auto new_mpi_manager =
                mpi_manager.split(color, mpi_manager.getRank());
            int reduce_root_rank = -1;

            if (mpi_manager.getRank() == i) {
                reduce_root_rank = new_mpi_manager.getRank();
            }

            mpi_manager.template Bcast<int>(reduce_root_rank, i);

            if (new_mpi_manager.getComm() != MPI_COMM_NULL) {
                new_mpi_manager.template Reduce<CFP_t>(
                    d_tmp->getData(), d_sv_prime->getData(), length_local,
                    reduce_root_rank, "sum");
            }
            PL_CUDA_IS_SUCCESS(cudaDeviceSynchronize());
            mpi_manager.Barrier();
        }
        sv.CopyGpuDataToGpuIn(d_sv_prime->getData(), d_sv_prime->getLength());
        mpi_manager.Barrier();
    }
};

} // namespace Pennylane::LightningGPU::Observables