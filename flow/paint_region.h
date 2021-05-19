#include <vector>
#include "flutter/fml/logging.h"
#include "third_party/skia/include/core/SkRect.h"

namespace flutter {

#ifdef FLUTTER_ENABLE_DIFF_CONTEXT

// Corresponds to area on the screen where the layer subtree has painted to.
//
// The area is used when adding damage of removed or dirty layer to overall
// damage.
//
// Because there is a PaintRegion for each layer, it must be able to represent
// the area with minimal overhead. This is accomplished by having one
// vector<SkRect> shared between all paint regions, and each paint region
// keeping begin and end index of rects relevant to particular subtree.
//
// All rects are in screen coordinates.
class PaintRegion {
 public:
  PaintRegion() = default;
  PaintRegion(std::shared_ptr<std::vector<SkRect>> rects,
              size_t from,
              size_t to,
              bool has_readback)
      : rects_(rects), from_(from), to_(to), has_readback_(has_readback) {}

  std::vector<SkRect>::const_iterator begin() const {
    FML_DCHECK(is_valid());
    return rects_->begin() + from_;
  }

  std::vector<SkRect>::const_iterator end() const {
    FML_DCHECK(is_valid());
    return rects_->begin() + to_;
  }

  // Compute bounds for this region
  SkRect ComputeBounds() const;

  bool is_valid() const { return rects_ != nullptr; }

  // Returns true if there is a layer in subtree represented by this region
  // that performs readback
  bool has_readback() const { return has_readback_; }

 private:
  std::shared_ptr<std::vector<SkRect>> rects_;
  size_t from_ = 0;
  size_t to_ = 0;
  bool has_readback_ = false;
};

#endif

}  // namespace flutter
