#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <iostream>
#include <numeric>
#include <vector>

#include <mpi.h>

#include "StateVectorKokkos.hpp"
#include "StateVectorKokkosMPI.hpp"

#include "output_utils.hpp"

namespace {
using namespace Pennylane;
using namespace Pennylane::LightningKokkos;
using namespace Pennylane::Gates;
using t_scale = std::milli;
using namespace BMUtils;

void normalize(std::vector<std::complex<double>> &vec) {
    double sum{0.0};
    for (std::size_t i = 0; i < vec.size(); i++) {
        sum += norm(vec[i]);
    }
    sum = std::sqrt(sum);
    for (std::size_t i = 0; i < vec.size(); i++) {
        vec[i] /= sum;
    }
}

std::vector<std::complex<double>> get_ascend_vector(const std::size_t nq) {
    constexpr std::size_t one{1U};
    std::size_t nsv = one << nq;
    std::vector<std::complex<double>> vec(nsv);
    for (std::size_t i = 0; i < vec.size(); i++) {
        vec[i] = std::complex<double>{static_cast<double>(i + 1)};
    }
    normalize(vec);
    return vec;
}

template <class ComplexT>
void print(const std::vector<ComplexT> &vec,
           const std::string &name = "vector") {
    std::cout << "Vector : " << name << " = np.array([" << std::endl;
    for (auto &e : vec) {
        std::cout << real(e) << " + 1j * " << imag(e) << std::endl;
    }
    std::cout << "])" << std::endl;
}

void print(StateVectorKokkosMPI<double> sv,
           const std::string &name = "statevector") {
    auto data = sv.getDataVector();
    if (0 == sv.get_mpi_rank()) {
        print(data, name);
    }
}

void print_basis_states(const std::size_t n) {
    constexpr std::size_t one{1U};
    for (std::size_t i = 0; i < one << n; i++) {
        StateVectorKokkosMPI<double> sv(n);
        sv.setBasisState(i);
        print(sv, "basis-" + std::to_string(i));
    }
}

} // namespace

int main(int argc, char *argv[]) {
    auto indices = prep_input_1q<unsigned int>(argc, argv);
    constexpr std::size_t run_avg = 1;
    std::string gate = "Hadamard";
    std::size_t nq = indices.q;
    std::vector<std::complex<double>> sv_data = get_ascend_vector(nq);

    // Create PennyLane Lightning statevector
    StateVectorKokkos<double> sv(sv_data);
    StateVectorKokkosMPI<double> svmpi(indices.q);
    // print(svmpi);
    print_basis_states(indices.q);

    // Create vector for run-times to average
    std::vector<double> times;
    times.reserve(run_avg);
    std::vector<std::size_t> targets{indices.t};

    // Apply the gates `run_avg` times on the indicated targets
    for (std::size_t i = 0; i < run_avg; i++) {
        TIMING(sv.applyOperation(gate, targets));
    }

    CSVOutput<decltype(indices), t_scale> csv(indices, gate,
                                              average_times(times));
    std::cout << csv << std::endl;

    int finflag;
    MPI_Finalized(&finflag);
    if (!finflag) {
        MPI_Finalize();
    }

    return 0;
}
