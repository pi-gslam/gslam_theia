// Copyright (C) 2014 The Regents of the University of California (Regents).
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//
//     * Neither the name of The Regents or University of California nor the
//       names of its contributors may be used to endorse or promote products
//       derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Please contact the author of this library if you have any questions.
// Author: Chris Sweeney (cmsweeney@cs.ucsb.edu)

#ifndef THEIA_SFM_CAMERA_REPROJECTION_ERROR_H_
#define THEIA_SFM_CAMERA_REPROJECTION_ERROR_H_

#include <ceres/ceres.h>
#include <ceres/rotation.h>
#include "theia/sfm/feature.h"
#include "theia/sfm/camera/camera.h"
#include "theia/sfm/camera/pinhole_camera_model.h"

namespace theia {

template <class CameraModel>
struct ReprojectionError {
 public:
  explicit ReprojectionError(const Feature& feature) : feature_(feature) {}

  template <typename T>
  bool operator()(const T* extrinsic_parameters,
                  const T* intrinsic_parameters,
                  const T* point,
                  T* reprojection_error) const {
    typedef Eigen::Matrix<T, 3, 1> Matrix3T;
    typedef Eigen::Map<const Matrix3T> ConstMap3T;

    static const T kVerySmallNumber(1e-8);

    // Remove the translation.
    Eigen::Matrix<T, 3, 1> adjusted_point =
        ConstMap3T(point) -
        point[3] * ConstMap3T(extrinsic_parameters + Camera::POSITION);

    // If the point is too close to the camera center then the point cannot be
    // constrained by triangulation. This is likely to only occur when a 3d
    // point is seen by 2 views and the camera center of 1 view lies on or neare
    // the optical axis of the other view.
    //
    // Since we do not know the camera model we cannot say that the point must
    // be in front of the camera (e.g., wide angle cameras that have > 180
    // degree FOV). Instead we simply force that the point is not near the
    // camera center.
    if (adjusted_point.squaredNorm() < kVerySmallNumber) {
      return false;
    }

    // Rotate the point to obtain the point in the camera coordinate system.
    T rotated_point[3];
    ceres::AngleAxisRotatePoint(extrinsic_parameters + Camera::ORIENTATION,
                                adjusted_point.data(),
                                rotated_point);

    // Apply the camera intrinsics to get the reprojected pixel.
    T reprojection[2];
    CameraModel::CameraToPixelCoordinates(intrinsic_parameters,
                                          rotated_point,
                                          reprojection);

    // Compute the reprojection error.
    reprojection_error[0] = reprojection[0] - feature_.x();
    reprojection_error[1] = reprojection[1] - feature_.y();
    return true;
  }

 private:
  const Feature feature_;
};

}  // namespace theia

#endif  // THEIA_SFM_CAMERA_REPROJECTION_ERROR_H_
