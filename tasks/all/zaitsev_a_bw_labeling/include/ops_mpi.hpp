#pragma once

#include <boost/mpi/communicator.hpp>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "all/zaitsev_a_bw_labeling/include/disjoint_set.hpp"
#include "core/task/include/task.hpp"

using Image = std::vector<uint8_t>;
using Length = unsigned int;
using Labels = std::vector<uint16_t>;
using LabelsList = std::vector<Labels>;
using Equivalency = std::map<std::uint16_t, std::set<std::uint16_t>>;
using Equivalencies = std::vector<Equivalency>;
using Ordinal = uint16_t;
using Ordinals = std::vector<Ordinal>;
using Replacements = std::vector<std::uint16_t>;
using DisjointSet = zaitsev_a_disjoint_set::DisjointSet<uint16_t>;

namespace zaitsev_a_labeling_mpi {
class Labeler : public ppc::core::Task {
  Length full_height_;
  std::vector<int> counts_;
  std::vector<int> displs_;
  Image image_;
  Labels labels_;
  Length width_;
  Length height_;
  Length size_;
  Length chunk_;
  boost::mpi::communicator world_;

  void Share();
  void Prepare();
  void Collect();
  void Sequenize();
  void UniteOnBorders();

  void LabelingRasterScan(Ordinals& ordinals);
  void GlobalizeLabels(Ordinals& ordinals);
  void UniteChunks();

 public:
  explicit Labeler(ppc::core::TaskDataPtr task_data) : Task(std::move(task_data)) {}
  bool PreProcessingImpl() override;
  bool ValidationImpl() override;
  bool RunImpl() override;
  bool PostProcessingImpl() override;
};

}  // namespace zaitsev_a_labeling_mpi