// Copyright (C) 2015 The Regents of the University of California (Regents).
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

#include "theia/sfm/feature_extractor_and_matcher.h"

#include <Eigen/Core>
#include <algorithm>
#include <glog/logging.h>
#include <memory>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "theia/image/descriptor/create_descriptor_extractor.h"
#include "theia/image/descriptor/descriptor_extractor.h"
#include "theia/image/image.h"
#include "theia/image/keypoint_detector/keypoint.h"
#include "theia/matching/create_feature_matcher.h"
#include "theia/matching/feature_correspondence.h"
#include "theia/matching/feature_matcher_options.h"
#include "theia/matching/image_pair_match.h"
#include "theia/sfm/camera_intrinsics_prior.h"
#include "theia/sfm/estimate_twoview_info.h"
#include "theia/sfm/exif_reader.h"
#include "theia/sfm/two_view_match_geometric_verification.h"
#include "theia/util/filesystem.h"
#include "theia/util/string.h"
#include "theia/util/threadpool.h"

namespace theia {
namespace {

void ExtractFeatures(const FeatureExtractorAndMatcher::Options& options,
                     const std::string& image_filepath,
                     const std::string& imagemask_filepath,
                     std::vector<Keypoint>* keypoints,
                     std::vector<Eigen::VectorXf>* descriptors) {
  static const float kMaskThreshold = 0.5;
  std::unique_ptr<FloatImage> image(new FloatImage(image_filepath));
  // We create these variable here instead of upon the construction of the
  // object so that they can be thread-safe. We *should* be able to use the
  // static thread_local keywords, but apparently Mac OS-X's version of clang
  // does not actually support it!
  //
  // TODO(cmsweeney): Change this so that each thread in the threadpool receives
  // exactly one object.
  std::unique_ptr<DescriptorExtractor> descriptor_extractor =
      CreateDescriptorExtractor(options.descriptor_extractor_type,
                                options.feature_density);

  // Exit if the descriptor extraction fails.
  if (!descriptor_extractor->DetectAndExtractDescriptors(
          *image, keypoints, descriptors)) {
    LOG(ERROR) << "Could not extract descriptors in image " << image_filepath;
    return;
  }

  if (imagemask_filepath.size() > 0) {
    std::unique_ptr<FloatImage> image_mask(new FloatImage(imagemask_filepath));
    // Check the size of the image and its associated mask.
    CHECK(image_mask->Width() == image->Width() &&
          image_mask->Height() == image->Height())
        << "The image and the mask don't have the same size. \n"
        << "- Image: " << image_filepath << "\t(" << image->Width() << " x "
        << image->Height() << ")\n"
        << "- Mask: " << imagemask_filepath << "\t(" << image_mask->Width()
        << " x " << image_mask->Height() << ")";

    // Convert the mask to grayscale.
    image_mask->ConvertToGrayscaleImage();
    // Remove keypoints according to the associated mask (remove kp. in black
    // part).
    for (int i = keypoints->size() - 1; i > -1; i--) {
      if (image_mask->BilinearInterpolate(
              keypoints->at(i).x(), keypoints->at(i).y(), 0) < kMaskThreshold) {
        keypoints->erase(keypoints->begin() + i);
        descriptors->erase(descriptors->begin() + i);
      }
    }
  }

  if (keypoints->size() > options.max_num_features) {
    keypoints->resize(options.max_num_features);
    descriptors->resize(options.max_num_features);
  }

  if (imagemask_filepath.size() > 0) {
    VLOG(1) << "Successfully extracted " << descriptors->size()
            << " features from image " << image_filepath
            << " with an image mask.";
  } else {
    VLOG(1) << "Successfully extracted " << descriptors->size()
            << " features from image " << image_filepath;
  }
}

}  // namespace

FeatureExtractorAndMatcher::FeatureExtractorAndMatcher(
    const FeatureExtractorAndMatcher::Options& options)
    : options_(options) {
  // Create the feature matcher.
  FeatureMatcherOptions matcher_options = options_.feature_matcher_options;
  matcher_options.num_threads = options_.num_threads;
  matcher_options.min_num_feature_matches = options_.min_num_inlier_matches;
  matcher_options.perform_geometric_verification = true;
  matcher_options.geometric_verification_options.min_num_inlier_matches =
      options_.min_num_inlier_matches;

  matcher_ = CreateFeatureMatcher(options_.matching_strategy, matcher_options);
}

bool FeatureExtractorAndMatcher::AddImage(const std::string& image_filepath) {
  image_filepaths_.emplace_back(image_filepath);
  return true;
}

bool FeatureExtractorAndMatcher::AddImage(
    const std::string& image_filepath,
    const CameraIntrinsicsPrior& intrinsics) {
  if (!AddImage(image_filepath)) {
    return false;
  }
  intrinsics_[image_filepath] = intrinsics;
  return true;
}

bool FeatureExtractorAndMatcher::AddMaskForFeaturesExtraction(
    const std::string& image_filepath, const std::string& mask_filepath) {
  image_masks_[image_filepath] = mask_filepath;
  VLOG(1) << "Image: " << image_filepath << " || "
          << "Associated mask: " << mask_filepath;
  return true;
}
void FeatureExtractorAndMatcher::SetPairsToMatch(
    const std::vector<std::pair<std::string, std::string> >& pairs_to_match) {
  // Convert the image filepaths to image filenames.
  std::vector<std::pair<std::string, std::string> > image_pairs;
  image_pairs.reserve(pairs_to_match.size());
  for (const auto& pair_to_match : pairs_to_match) {
    std::string image1_filename;
    CHECK(GetFilenameFromFilepath(pair_to_match.first, true, &image1_filename));
    std::string image2_filename;
    CHECK(
        GetFilenameFromFilepath(pair_to_match.second, true, &image2_filename));
    image_pairs.emplace_back(image1_filename, image2_filename);
  }

  matcher_->SetImagePairsToMatch(image_pairs);
}

// Performs feature matching between all images provided by the image
// filepaths. Features are extracted and matched between the images according to
// the options passed in. Only matches that have passed geometric verification
// are kept. EXIF data is parsed to determine the camera intrinsics if
// available.
void FeatureExtractorAndMatcher::ExtractAndMatchFeatures(
    std::vector<CameraIntrinsicsPrior>* intrinsics,
    std::vector<ImagePairMatch>* matches) {
  CHECK_NOTNULL(intrinsics)->resize(image_filepaths_.size());
  CHECK_NOTNULL(matches);
  CHECK_NOTNULL(matcher_.get());

  // For each image, process the features and add it to the matcher.
  const int num_threads =
      std::min(options_.num_threads, static_cast<int>(image_filepaths_.size()));
  std::unique_ptr<ThreadPool> thread_pool(new ThreadPool(num_threads));
  for (int i = 0; i < image_filepaths_.size(); i++) {
    if (!FileExists(image_filepaths_[i])) {
      LOG(ERROR) << "Could not extract features for " << image_filepaths_[i]
                 << " because the file cannot be found.";
      continue;
    }
    thread_pool->Add(&FeatureExtractorAndMatcher::ProcessImage, this, i);
  }
  // This forces all tasks to complete before proceeding.
  thread_pool.reset(nullptr);

  // After all threads complete feature extraction, perform matching.

  // Perform the matching.
  LOG(INFO) << "Matching images...";
  matcher_->MatchImages(matches);

  // Add the intrinsics to the output.
  for (int i = 0; i < image_filepaths_.size(); i++) {
    (*intrinsics)[i] = FindOrDie(intrinsics_, image_filepaths_[i]);
  }
}

void FeatureExtractorAndMatcher::ProcessImage(const int i) {
  const std::string& image_filepath = image_filepaths_[i];

  // Get the camera intrinsics prior if it was provided.
  CameraIntrinsicsPrior intrinsics =
      FindWithDefault(intrinsics_, image_filepath, CameraIntrinsicsPrior());

  // Get the associated mask if it was provided.
  const std::string mask_filepath =
      FindWithDefault(image_masks_, image_filepath, "");

  // Extract an EXIF focal length if it was not provided.
  if (!intrinsics.focal_length.is_set) {
    CHECK(exif_reader_.ExtractEXIFMetadata(image_filepath, &intrinsics));

    // If the focal length still could not be extracted, set it to a reasonable
    // value based on a median viewing angle.
    if (!options_.only_calibrated_views && !intrinsics.focal_length.is_set) {
      VLOG(2) << "Exif was not detected. Setting it to a reasonable value.";
      intrinsics.focal_length.is_set = true;
      intrinsics.focal_length.value[0] =
          1.2 * static_cast<double>(
                    std::max(intrinsics.image_width, intrinsics.image_height));
    }

    std::lock_guard<std::mutex> lock(intrinsics_mutex_);
    // Insert or update the value of the intrinsics.
    intrinsics_[image_filepath] = intrinsics;
  }

  // Early exit if no EXIF calibration exists and we are only processing
  // calibration views.
  if (options_.only_calibrated_views && !intrinsics.focal_length.is_set) {
    LOG(INFO) << "Image " << image_filepath
              << " did not contain an EXIF focal length. Skipping this image.";
    return;
  } else {
    LOG(INFO) << "Image " << image_filepath
              << " is initialized with the focal length: "
              << intrinsics.focal_length.value[0];
  }

  // Get the image filename without the directory.
  std::string image_filename;
  CHECK(GetFilenameFromFilepath(image_filepath, true, &image_filename));

  // Get the feature filepath based on the image filename.
  std::string output_dir =
      options_.feature_matcher_options.keypoints_and_descriptors_output_dir;
  AppendTrailingSlashIfNeeded(&output_dir);
  const std::string feature_filepath =
      output_dir + image_filename + ".features";

  // If the feature file already exists, skip the feature extraction.
  if (options_.feature_matcher_options.match_out_of_core &&
      FileExists(feature_filepath)) {
    std::lock_guard<std::mutex> lock(matcher_mutex_);
    matcher_->AddImage(image_filename, intrinsics);
    return;
  }

  // Extract Features.
  std::vector<Keypoint> keypoints;
  std::vector<Eigen::VectorXf> descriptors;
  ExtractFeatures(
      options_, image_filepath, mask_filepath, &keypoints, &descriptors);

  // Add the relevant image and feature data to the feature matcher. This allows
  // the feature matcher to control fine-grained things like multi-threading and
  // caching. For instance, the matcher may choose to write the descriptors to
  // disk and read them back as needed.
  std::lock_guard<std::mutex> lock(matcher_mutex_);
  matcher_->AddImage(image_filename, keypoints, descriptors, intrinsics);
}

}  // namespace theia
