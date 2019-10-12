// Copyright (C) 2016 The Regents of the University of California (Regents).
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

#include "theia/sfm/camera/fisheye_camera_model.h"

#include <ceres/rotation.h>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <glog/logging.h>

#include "theia/sfm/bundle_adjustment/bundle_adjustment.h"
#include "theia/sfm/camera_intrinsics_prior.h"
#include "theia/sfm/camera/projection_matrix_utils.h"

namespace theia {

using Eigen::AngleAxisd;
using Eigen::Map;
using Eigen::Matrix;
using Eigen::Matrix3d;
using Eigen::Vector2d;
using Eigen::Vector3d;
using Eigen::Vector4d;

FisheyeCameraModel::FisheyeCameraModel() {
  parameters_.resize(kIntrinsicsSize);
  SetFocalLength(1.0);
  SetPrincipalPoint(0.0, 0.0);
  SetParameter(ASPECT_RATIO, 1.0);
  SetParameter(SKEW, 0.0);
  SetParameter(RADIAL_DISTORTION_1, 0.0);
  SetParameter(RADIAL_DISTORTION_2, 0.0);
  SetParameter(RADIAL_DISTORTION_3, 0.0);
  SetParameter(RADIAL_DISTORTION_4, 0.0);
}

int FisheyeCameraModel::NumParameters() const {return kIntrinsicsSize;}

// Returns the camera model type of the object.
CameraIntrinsicsModelType FisheyeCameraModel::Type() const {
  return CameraIntrinsicsModelType::FISHEYE;
}

// Set the intrinsic camera parameters from the priors.
void FisheyeCameraModel::SetFromCameraIntrinsicsPriors(
    const CameraIntrinsicsPrior& prior) {
  // Set the focal length.
  if (prior.focal_length.is_set) {
    SetFocalLength(prior.focal_length.value[0]);
  } else if (prior.image_width != 0.0 && prior.image_height != 0.0) {
    // NOTE: A scaling of 0.4 times the max image dimension was empircally
    // observed to be a decent initialization by csweeney.
    static const double kFocalLengthScaleFactor = 0.4;
    SetFocalLength(
        kFocalLengthScaleFactor *
        static_cast<double>(std::max(prior.image_width, prior.image_height)));
  }

  // Set the principal point.
  if (prior.principal_point.is_set) {
    SetPrincipalPoint(prior.principal_point.value[0],
                      prior.principal_point.value[1]);
  } else if (prior.image_width != 0.0 && prior.image_height != 0.0) {
    SetPrincipalPoint(prior.image_width / 2.0, prior.image_height / 2.0);
  }

  // Set aspect ratio if available.
  if (prior.aspect_ratio.is_set) {
    SetParameter(ASPECT_RATIO, prior.aspect_ratio.value[0]);
  }

  // Set skew if available.
  if (prior.skew.is_set) {
    SetParameter(SKEW, prior.skew.value[0]);
  }

  // Set radial distortion if available.
  if (prior.radial_distortion.is_set) {
    SetParameter(RADIAL_DISTORTION_1, prior.radial_distortion.value[0]);
    SetParameter(RADIAL_DISTORTION_2, prior.radial_distortion.value[1]);
    SetParameter(RADIAL_DISTORTION_3, prior.radial_distortion.value[2]);
    SetParameter(RADIAL_DISTORTION_4, prior.radial_distortion.value[3]);
  }
}

CameraIntrinsicsPrior FisheyeCameraModel::CameraIntrinsicsPriorFromIntrinsics()
    const {
  CameraIntrinsicsPrior prior;
  prior.camera_intrinsics_model_type =
      CameraIntrinsicsModelTypeToString(Type());
  prior.focal_length.is_set = true;
  prior.focal_length.value[0] = FocalLength();
  prior.principal_point.is_set = true;
  prior.principal_point.value[0] = PrincipalPointX();
  prior.principal_point.value[1] = PrincipalPointY();
  prior.aspect_ratio.is_set = true;
  prior.aspect_ratio.value[0] = AspectRatio();
  prior.skew.is_set = true;
  prior.skew.value[0] = Skew();
  prior.radial_distortion.is_set = true;
  prior.radial_distortion.value[0] = RadialDistortion1();
  prior.radial_distortion.value[1] = RadialDistortion2();
  prior.radial_distortion.value[2] = RadialDistortion3();
  prior.radial_distortion.value[3] = RadialDistortion4();

  return prior;
}

// Returns the indices of the parameters that will be optimized during bundle
// adjustment.
std::vector<int> FisheyeCameraModel::GetSubsetFromOptimizeIntrinsicsType(
    const OptimizeIntrinsicsType& intrinsics_to_optimize) const {
  std::vector<int> constant_intrinsics;
  if (intrinsics_to_optimize == OptimizeIntrinsicsType::ALL) {
    return constant_intrinsics;
  }

  if ((intrinsics_to_optimize &
      OptimizeIntrinsicsType::FOCAL_LENGTH) == OptimizeIntrinsicsType::NONE) {
    constant_intrinsics.emplace_back(FOCAL_LENGTH);
  }
  if ((intrinsics_to_optimize & OptimizeIntrinsicsType::ASPECT_RATIO) ==
      OptimizeIntrinsicsType::NONE) {
    constant_intrinsics.emplace_back(ASPECT_RATIO);
  }
  if ((intrinsics_to_optimize & OptimizeIntrinsicsType::SKEW) ==
      OptimizeIntrinsicsType::NONE) {
    constant_intrinsics.emplace_back(SKEW);
  }
  if ((intrinsics_to_optimize & OptimizeIntrinsicsType::PRINCIPAL_POINTS) ==
      OptimizeIntrinsicsType::NONE) {
    constant_intrinsics.emplace_back(PRINCIPAL_POINT_X);
    constant_intrinsics.emplace_back(PRINCIPAL_POINT_Y);
  }
  if ((intrinsics_to_optimize & OptimizeIntrinsicsType::RADIAL_DISTORTION) ==
      OptimizeIntrinsicsType::NONE) {
    constant_intrinsics.emplace_back(RADIAL_DISTORTION_1);
    constant_intrinsics.emplace_back(RADIAL_DISTORTION_2);
    constant_intrinsics.emplace_back(RADIAL_DISTORTION_3);
    constant_intrinsics.emplace_back(RADIAL_DISTORTION_4);
  }
  return constant_intrinsics;
}

void FisheyeCameraModel::GetCalibrationMatrix(Matrix3d* kmatrix) const {
  IntrinsicsToCalibrationMatrix(parameters_[FOCAL_LENGTH],
                                parameters_[SKEW],
                                parameters_[ASPECT_RATIO],
                                parameters_[PRINCIPAL_POINT_X],
                                parameters_[PRINCIPAL_POINT_Y],
                                kmatrix);
}

void FisheyeCameraModel::PrintIntrinsics() const {
  LOG(INFO) << "Camera model type: "
            << CameraIntrinsicsModelTypeToString(Type())
            << "\nFocal length (pixels): " << FocalLength()
            << "\nPrincipal Point (px, py) = (" << PrincipalPointX() << ", "
            << PrincipalPointY() << ")"
            << "\nSkew: " << Skew() << "\nAspect Ratio: " << AspectRatio()
            << "\nRadialDistortion: " << RadialDistortion1() << ", "
            << RadialDistortion2() << ", " << RadialDistortion3() << ", "
            << RadialDistortion4();
}

// ----------------------- Getter and Setter methods ---------------------- //

void FisheyeCameraModel::SetAspectRatio(const double aspect_ratio) {
  parameters_[ASPECT_RATIO] = aspect_ratio;
}
double FisheyeCameraModel::AspectRatio() const {
  return parameters_[ASPECT_RATIO];
}

void FisheyeCameraModel::SetSkew(const double skew) {
  parameters_[SKEW] = skew;
}

double FisheyeCameraModel::Skew() const {
  return parameters_[SKEW];
}

void FisheyeCameraModel::SetRadialDistortion(const double radial_distortion_1,
                                             const double radial_distortion_2,
                                             const double radial_distortion_3,
                                             const double radial_distortion_4) {
  parameters_[RADIAL_DISTORTION_1] = radial_distortion_1;
  parameters_[RADIAL_DISTORTION_2] = radial_distortion_2;
  parameters_[RADIAL_DISTORTION_3] = radial_distortion_3;
  parameters_[RADIAL_DISTORTION_4] = radial_distortion_4;
}

double FisheyeCameraModel::RadialDistortion1() const {
  return parameters_[RADIAL_DISTORTION_1];
}

double FisheyeCameraModel::RadialDistortion2() const {
  return parameters_[RADIAL_DISTORTION_2];
}

double FisheyeCameraModel::RadialDistortion3() const {
  return parameters_[RADIAL_DISTORTION_3];
}

double FisheyeCameraModel::RadialDistortion4() const {
  return parameters_[RADIAL_DISTORTION_4];
}

}  // namespace theia
