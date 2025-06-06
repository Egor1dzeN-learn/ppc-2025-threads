#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <utility>
#include <vector>

#include "core/task/include/task.hpp"
#include "tbb/kondratev_ya_ccs_complex_multiplication/include/ops_tbb.hpp"

namespace {
std::vector<std::complex<double>> GenerateRandomSparseMatrix(std::pair<int, int> sizes, double density) {
  int rows = sizes.first;
  int cols = sizes.second;

  std::vector<std::complex<double>> matrix(rows * cols, std::complex<double>(0.0, 0.0));

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<double> dist(-10.0, 10.0);

  auto count_not_zero_elements = static_cast<int>(rows * cols * density);
  std::transform(matrix.begin(), matrix.begin() + count_not_zero_elements, matrix.begin(),
                 [&](auto &) { return std::complex<double>(dist(gen), dist(gen)); });

  std::shuffle(matrix.begin(), matrix.end(), gen);

  return matrix;
}

kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix ConvertToCCS(
    const std::vector<std::complex<double>> &dense_matrix, std::pair<int, int> sizes) {
  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix sparse(sizes);

  for (int col = 0; col < sparse.cols; col++) {
    sparse.col_ptrs[col] = static_cast<int>(sparse.values.size());

    for (int row = 0; row < sparse.rows; row++) {
      int idx = (row * sparse.cols) + col;

      if (kondratev_ya_ccs_complex_multiplication_tbb::IsZero(dense_matrix[idx])) {
        continue;
      }

      sparse.values.emplace_back(dense_matrix[idx]);
      sparse.row_index.emplace_back(row);
    }
  }

  sparse.col_ptrs[sparse.cols] = static_cast<int>(sparse.values.size());

  return sparse;
}

bool IsComplexVectorEqual(const std::vector<std::complex<double>> &a, const std::vector<std::complex<double>> &b) {
  return std::ranges::equal(
      a, b, [](const auto &x, const auto &y) { return kondratev_ya_ccs_complex_multiplication_tbb::IsEqual(x, y); });
}

std::vector<std::complex<double>> ClassicMultiplyMatrices(const std::vector<std::complex<double>> &a,
                                                          std::pair<int, int> a_size,
                                                          const std::vector<std::complex<double>> &b,
                                                          std::pair<int, int> b_size) {
  if (a_size.second != b_size.first) {
    return {};
  }

  const int m = a_size.first;
  const int n = b_size.second;
  const int k = a_size.second;

  std::vector<std::complex<double>> result(m * n, std::complex<double>(0.0, 0.0));

  for (int i = 0; i < m; i++) {
    for (int j = 0; j < n; j++) {
      for (int h = 0; h < k; h++) {
        result[(i * n) + j] += a[(i * k) + h] * b[(h * n) + j];
      }
    }
  }

  return result;
}

void CCSExpectEqual(const kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix &a,
                    const kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix &b) {
  EXPECT_TRUE(IsComplexVectorEqual(a.values, b.values));
  EXPECT_EQ(a.row_index, b.row_index);
  EXPECT_EQ(a.col_ptrs, b.col_ptrs);
  EXPECT_EQ(a.rows, b.rows);
  EXPECT_EQ(a.cols, b.cols);
}

struct Matrices {
  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix &in1;
  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix &in2;
  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix &out;
};

void RunTest(Matrices matrices) {
  auto task_data_tbb = std::make_shared<ppc::core::TaskData>();

  task_data_tbb->inputs.emplace_back(reinterpret_cast<uint8_t *>(&matrices.in1));
  task_data_tbb->inputs.emplace_back(reinterpret_cast<uint8_t *>(&matrices.in2));
  task_data_tbb->inputs_count.emplace_back(2);

  task_data_tbb->outputs.emplace_back(reinterpret_cast<uint8_t *>(&matrices.out));
  task_data_tbb->outputs_count.emplace_back(1);

  kondratev_ya_ccs_complex_multiplication_tbb::TestTaskTBB test_task_tbbuential(task_data_tbb);
  ASSERT_TRUE(test_task_tbbuential.Validation());
  ASSERT_TRUE(test_task_tbbuential.PreProcessing());
  ASSERT_TRUE(test_task_tbbuential.Run());
  ASSERT_TRUE(test_task_tbbuential.PostProcessing());
}

}  // namespace

TEST(kondratev_ya_ccs_complex_multiplication_tbb, matrix_convert_to_ccs) {
  // clang-format off
  std::vector<std::complex<double>> dense_matrix = {
    {1.0, 0.0}, {0.0, 0.0}, {2.0, 1.0}, 
    {0.0, 0.0}, {3.0, -1.0}, {0.0, 0.0}, 
    {4.0, 0.0}, {0.0, 0.0}, {5.0, 2.0}
  };
  // clang-format on

  auto expected_values = std::vector<std::complex<double>>{
      {1.0, 0.0}, {4.0, 0.0}, {3.0, -1.0}, {2.0, 1.0}, {5.0, 2.0},
  };
  auto expected_row_index = std::vector<int>{0, 2, 1, 0, 2};
  auto expected_col_ptrs = std::vector<int>{0, 2, 3, 5};

  auto ccs = ConvertToCCS(dense_matrix, {3, 3});

  EXPECT_TRUE(IsComplexVectorEqual(ccs.values, expected_values));
  EXPECT_EQ(ccs.row_index, expected_row_index);
  EXPECT_EQ(ccs.col_ptrs, expected_col_ptrs);
}

TEST(kondratev_ya_ccs_complex_multiplication_tbb, generate_random_sparse_matrix) {
  int rows = 5;
  int cols = 5;
  double density = 0.2;
  auto sparse_matrix = GenerateRandomSparseMatrix({rows, cols}, density);

  int expected_count_not_zero_elements = static_cast<int>(rows * cols * density);
  int current_not_zero_elements = 0;
  for (size_t i = 0; i < sparse_matrix.size(); i++) {
    if (!kondratev_ya_ccs_complex_multiplication_tbb::IsZero(sparse_matrix[i])) {
      current_not_zero_elements++;
    }
  }

  EXPECT_EQ(current_not_zero_elements, expected_count_not_zero_elements);
}

TEST(kondratev_ya_ccs_complex_multiplication_tbb, test_classic_matrix_multiplication) {
  // clang-format off
  std::vector<std::complex<double>> a = {
    {1.0, 1.0}, {0.0, 0.0}, 
    {0.0, 0.0}, {2.0, -1.0}
  };

  std::vector<std::complex<double>> b = {
    {1.0, -1.0}, {2.0, 0.0}, 
    {0.0, 1.0},  {3.0, 1.0}
  };

  std::vector<std::complex<double>> expected = {
    {2.0, 0.0}, {2.0, 2.0}, 
    {1.0, 2.0}, {7.0, -1.0}
  };
  // clang-format on

  auto result = ClassicMultiplyMatrices(a, {2, 2}, b, {2, 2});
  EXPECT_TRUE(IsComplexVectorEqual(expected, result));
}

TEST(kondratev_ya_ccs_complex_multiplication_tbb, test_empty_matrices) {
  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix a;
  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix b;

  auto task_data = std::make_shared<ppc::core::TaskData>();
  task_data->inputs.emplace_back(reinterpret_cast<uint8_t *>(&a));
  task_data->inputs.emplace_back(reinterpret_cast<uint8_t *>(&b));
  task_data->inputs_count.emplace_back(2);

  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix c;
  task_data->outputs.emplace_back(reinterpret_cast<uint8_t *>(&c));
  task_data->outputs_count.emplace_back(1);

  kondratev_ya_ccs_complex_multiplication_tbb::TestTaskTBB task(task_data);
  EXPECT_TRUE(task.ValidationImpl());
  EXPECT_FALSE(task.PreProcessingImpl());
}

TEST(kondratev_ya_ccs_complex_multiplication_tbb, test_single_element) {
  auto task_data = std::make_shared<ppc::core::TaskData>();
  kondratev_ya_ccs_complex_multiplication_tbb::TestTaskTBB task(task_data);

  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix a({1, 1});
  a.values = {{2.0, 1.0}};
  a.row_index = {0};
  a.col_ptrs = {0, 1};

  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix b({1, 1});
  b.values = {{3.0, -1.0}};
  b.row_index = {0};
  b.col_ptrs = {0, 1};

  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix expected({1, 1});
  expected.values = {{7.0, 1.0}};
  expected.row_index = {0};
  expected.col_ptrs = {0, 1};

  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix c({2, 2});
  RunTest({.in1 = a, .in2 = b, .out = c});
  CCSExpectEqual(c, expected);
}

TEST(kondratev_ya_ccs_complex_multiplication_tbb, test_small_ccs_multiplication) {
  // Матрица A (2x3):
  // [1+0i  0    2+1i]
  // [0     3-1i 0   ]
  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix a({2, 3});
  a.values = {{1.0, 0.0}, {3.0, -1.0}, {2.0, 1.0}};
  a.row_index = {0, 1, 0};
  a.col_ptrs = {0, 1, 2, 3};

  // Матрица B (3x2):
  // [1+0i  2+0i]
  // [0     3-1i]
  // [2+1i  0   ]
  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix b({3, 2});
  b.values = {{1.0, 0.0}, {2.0, 1.0}, {2.0, 0.0}, {3.0, -1.0}};
  b.row_index = {0, 2, 0, 1};
  b.col_ptrs = {0, 2, 4};

  // Ожидаемый результат (2x2):
  // [4+4i  2+0i]
  // [0     8-6i]
  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix expected({2, 2});
  expected.values = {{4.0, 4.0}, {2.0, 0.0}, {8.0, -6.0}};
  expected.row_index = {0, 0, 1};
  expected.col_ptrs = {0, 1, 3};

  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix c({2, 2});
  RunTest({.in1 = a, .in2 = b, .out = c});
  CCSExpectEqual(c, expected);
}

TEST(kondratev_ya_ccs_complex_multiplication_tbb, random_matrix_multiplication) {
  int rows = 5;
  int cols = 5;
  double density = 0.2;

  auto a = GenerateRandomSparseMatrix({rows, cols}, density);
  auto b = GenerateRandomSparseMatrix({rows, cols}, density);

  auto ccs_a = ConvertToCCS(a, {rows, cols});
  auto ccs_b = ConvertToCCS(b, {rows, cols});
  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix ccs_c({rows, cols});

  auto expected = ClassicMultiplyMatrices(a, {rows, cols}, b, {rows, cols});
  auto ccs_expected = ConvertToCCS(expected, {rows, cols});

  RunTest({.in1 = ccs_a, .in2 = ccs_b, .out = ccs_c});
  CCSExpectEqual(ccs_c, ccs_expected);
}

TEST(kondratev_ya_ccs_complex_multiplication_tbb, test_incompatible_matrix_sizes) {
  auto a = GenerateRandomSparseMatrix({3, 2}, 0.2);
  auto b = GenerateRandomSparseMatrix({3, 4}, 0.2);

  auto ccs_a = ConvertToCCS(a, {3, 2});
  auto ccs_b = ConvertToCCS(b, {3, 4});
  kondratev_ya_ccs_complex_multiplication_tbb::CCSMatrix c({3, 4});

  auto task_data = std::make_shared<ppc::core::TaskData>();
  task_data->inputs.emplace_back(reinterpret_cast<uint8_t *>(&ccs_a));
  task_data->inputs.emplace_back(reinterpret_cast<uint8_t *>(&ccs_b));
  task_data->inputs_count.emplace_back(2);
  task_data->outputs.emplace_back(reinterpret_cast<uint8_t *>(&c));
  task_data->outputs_count.emplace_back(1);

  kondratev_ya_ccs_complex_multiplication_tbb::TestTaskTBB task(task_data);

  EXPECT_TRUE(task.ValidationImpl());
  EXPECT_FALSE(task.PreProcessingImpl());
}
