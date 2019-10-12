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

#include "theia/sfm/estimate_twoview_info.h"

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <glog/logging.h>

#include <vector>

#include "theia/matching/feature_correspondence.h"
#include "theia/sfm/camera/camera.h"
#include "theia/sfm/camera_intrinsics_prior.h"
#include "theia/sfm/estimators/estimate_relative_pose.h"
#include "theia/sfm/estimators/estimate_uncalibrated_relative_pose.h"
#include "theia/sfm/pose/util.h"
#include "theia/sfm/reconstruction_estimator_utils.h"
#include "theia/sfm/set_camera_intrinsics_from_priors.h"
#include "theia/sfm/triangulation/triangulation.h"
#include "theia/sfm/twoview_info.h"
#include "theia/sfm/types.h"
#include "theia/sfm/visibility_pyramid.h"
#include "theia/solvers/sample_consensus_estimator.h"

namespace theia {

using Eigen::AngleAxisd;
using Eigen::Matrix3d;
using Eigen::Vector2d;
using Eigen::Vector3d;

namespace {

// Normalizes the image features by the camera intrinsics.
void NormalizeFeatures(
    const CameraIntrinsicsPrior& prior1,
    const CameraIntrinsicsPrior& prior2,
    const std::vector<FeatureCorrespondence>& correspondences,
    std::vector<FeatureCorrespondence>* normalized_correspondences) {
  CHECK_NOTNULL(normalized_correspondences)->clear();

  Camera camera1, camera2;
  camera1.SetFromCameraIntrinsicsPriors(prior1);
  camera2.SetFromCameraIntrinsicsPriors(prior2);
  // If no focal length prior is given, the SetFromCameraIntrinsicsPrior method
  // will set the focal length to a reasonable guess. However, for cameras with
  // no focal length priors we DO NOT want the feature normalization below to
  // divide by the focal length, so we must reset the focal lengths to 1.0 so
  // that the feature normalization is unaffected.
  if (!prior1.focal_length.is_set || !prior2.focal_length.is_set) {
    camera1.SetFocalLength(1.0);
    camera2.SetFocalLength(1.0);
  }

  normalized_correspondences->reserve(correspondences.size());
  for (const FeatureCorrespondence& correspondence : correspondences) {
    FeatureCorrespondence normalized_correspondence;
    const Eigen::Vector3d normalized_feature1 =
        camera1.PixelToNormalizedCoordinates(correspondence.feature1);
    normalized_correspondence.feature1 = normalized_feature1.hnormalized();

    const Eigen::Vector3d normalized_feature2 =
        camera2.PixelToNormalizedCoordinates(correspondence.feature2);
    normalized_correspondence.feature2 = normalized_feature2.hnormalized();

    normalized_correspondences->emplace_back(normalized_correspondence);
  }
}

// Compute the visibility score of the inliers in the images.
int ComputeVisibilityScoreOfInliers(
    const CameraIntrinsicsPrior& intrinsics1,
    const CameraIntrinsicsPrior& intrinsics2,
    const std::vector<FeatureCorrespondence>& correspondences,
    const std::vector<int>& inlier_indices) {
  static const int kNumPyramidLevels = 6;
  // If the image dimensions are not available, do not make any assumptions
  // about what they might be. Instead, we return the number of inliers as a
  // default.
  if (intrinsics1.image_width == 0 || intrinsics1.image_height == 0 ||
      intrinsics2.image_width == 0 || intrinsics2.image_height == 0) {
    return inlier_indices.size();
  }

  // Compute the visibility score for all inliers.
  VisibilityPyramid pyramid1(
      intrinsics1.image_width, intrinsics1.image_height, kNumPyramidLevels);
  VisibilityPyramid pyramid2(
      intrinsics2.image_width, intrinsics2.image_height, kNumPyramidLevels);
  for (const int i : inlier_indices) {
    const FeatureCorrespondence& match = correspondences[i];
    pyramid1.AddPoint(match.feature1);
    pyramid2.AddPoint(match.feature2);
  }
  // Return the summed score.
  return pyramid1.ComputeScore() + pyramid2.ComputeScore();
}

bool EstimateTwoViewInfoCalibrated(
    const EstimateTwoViewInfoOptions& options,
    const CameraIntrinsicsPrior& intrinsics1,
    const CameraIntrinsicsPrior& intrinsics2,
    const std::vector<FeatureCorrespondence>& correspondences,
    TwoViewInfo* twoview_info,
    std::vector<int>* inlier_indices) {
  // Normalize features w.r.t focal length.
  std::vector<FeatureCorrespondence> normalized_correspondences;
  NormalizeFeatures(
      intrinsics1, intrinsics2, correspondences, &normalized_correspondences);

  // Set the ransac parameters.
  RansacParameters ransac_options;
  ransac_options.rng = options.rng;
  ransac_options.failure_probability = 1.0 - options.expected_ransac_confidence;
  ransac_options.min_iterations = options.min_ransac_iterations;
  ransac_options.max_iterations = options.max_ransac_iterations;

  // Compute the sampson error threshold to account for the resolution of the
  // images.
  const double max_sampson_error_pixels1 =
      ComputeResolutionScaledThreshold(options.max_sampson_error_pixels,
                                       intrinsics1.image_width,
                                       intrinsics1.image_height);
  const double max_sampson_error_pixels2 =
      ComputeResolutionScaledThreshold(options.max_sampson_error_pixels,
                                       intrinsics2.image_width,
                                       intrinsics2.image_height);
  ransac_options.error_thresh =
      max_sampson_error_pixels1 * max_sampson_error_pixels2 /
      (intrinsics1.focal_length.value[0] * intrinsics2.focal_length.value[0]);
  ransac_options.use_mle = options.use_mle;

  RelativePose relative_pose;
  RansacSummary summary;
  if (!EstimateRelativePose(ransac_options,
                            options.ransac_type,
                            normalized_correspondences,
                            &relative_pose,
                            &summary)) {
    return false;
  }

  AngleAxisd rotation(relative_pose.rotation);

  // Set the twoview info.
  twoview_info->rotation_2 = rotation.angle() * rotation.axis();
  twoview_info->position_2 = relative_pose.position;
  twoview_info->focal_length_1 = intrinsics1.focal_length.value[0];
  twoview_info->focal_length_2 = intrinsics2.focal_length.value[0];
  twoview_info->num_verified_matches = summary.inliers.size();
  twoview_info->visibility_score = ComputeVisibilityScoreOfInliers(
      intrinsics1, intrinsics2, correspondences, *inlier_indices);

  *inlier_indices = summary.inliers;

  return true;
}

bool EstimateTwoViewInfoUncalibrated(
    const EstimateTwoViewInfoOptions& options,
    const CameraIntrinsicsPrior& intrinsics1,
    const CameraIntrinsicsPrior& intrinsics2,
    const std::vector<FeatureCorrespondence>& correspondences,
    TwoViewInfo* twoview_info,
    std::vector<int>* inlier_indices) {
  // Normalize features w.r.t principal point.
  std::vector<FeatureCorrespondence> centered_correspondences;
  NormalizeFeatures(
      intrinsics1, intrinsics2, correspondences, &centered_correspondences);

  // Set the ransac parameters.
  RansacParameters ransac_options;
  ransac_options.rng = options.rng;
  ransac_options.failure_probability = 1.0 - options.expected_ransac_confidence;
  ransac_options.min_iterations = options.min_ransac_iterations;
  ransac_options.max_iterations = options.max_ransac_iterations;

  // Compute the sampson error threshold to account for the resolution of the
  // images.
  const double max_sampson_error_pixels1 =
      ComputeResolutionScaledThreshold(options.max_sampson_error_pixels,
                                       intrinsics1.image_width,
                                       intrinsics1.image_height);
  const double max_sampson_error_pixels2 =
      ComputeResolutionScaledThreshold(options.max_sampson_error_pixels,
                                       intrinsics2.image_width,
                                       intrinsics2.image_height);
  ransac_options.error_thresh =
      max_sampson_error_pixels1 * max_sampson_error_pixels2;

  UncalibratedRelativePose relative_pose;
  RansacSummary summary;
  if (!EstimateUncalibratedRelativePose(ransac_options,
                                        options.ransac_type,
                                        centered_correspondences,
                                        &relative_pose,
                                        &summary)) {
    return false;
  }

  AngleAxisd rotation(relative_pose.rotation);

  // Set the twoview info.
  twoview_info->rotation_2 = rotation.angle() * rotation.axis();
  twoview_info->position_2 = relative_pose.position;
  twoview_info->focal_length_1 = relative_pose.focal_length1;
  twoview_info->focal_length_2 = relative_pose.focal_length2;

  // Get the number of verified features.
  twoview_info->num_verified_matches = summary.inliers.size();
  twoview_info->visibility_score = ComputeVisibilityScoreOfInliers(
      intrinsics1, intrinsics2, correspondences, *inlier_indices);
  *inlier_indices = summary.inliers;

  return true;
}

}  // namespace

bool EstimateTwoViewInfo(
    const EstimateTwoViewInfoOptions& options,
    const CameraIntrinsicsPrior& intrinsics1,
    const CameraIntrinsicsPrior& intrinsics2,
    const std::vector<FeatureCorrespondence>& correspondences,
    TwoViewInfo* twoview_info,
    std::vector<int>* inlier_indices) {
  CHECK_NOTNULL(twoview_info);
  CHECK_NOTNULL(inlier_indices)->clear();

  // Case where both views are calibrated.
  if (intrinsics1.focal_length.is_set && intrinsics2.focal_length.is_set) {
    return EstimateTwoViewInfoCalibrated(options,
                                         intrinsics1,
                                         intrinsics2,
                                         correspondences,
                                         twoview_info,
                                         inlier_indices);
  }

  // Only one of the focal lengths is set.
  if (intrinsics1.focal_length.is_set || intrinsics2.focal_length.is_set) {
    LOG(WARNING) << "Solving for two view infos when exactly one view is "
                    "calibrated has not been implemented yet. Treating both "
                    "views as uncalibrated instead.";
    return EstimateTwoViewInfoUncalibrated(options,
                                           intrinsics1,
                                           intrinsics2,
                                           correspondences,
                                           twoview_info,
                                           inlier_indices);
  }

  // Assume both views are uncalibrated.
  return EstimateTwoViewInfoUncalibrated(options,
                                         intrinsics1,
                                         intrinsics2,
                                         correspondences,
                                         twoview_info,
                                         inlier_indices);
}

}  // namespace theia
