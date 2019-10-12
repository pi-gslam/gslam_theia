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

#include "gtest/gtest.h"

#include "theia/sfm/reconstruction.h"
#include "theia/util/map_util.h"
#include "theia/util/stringprintf.h"

namespace theia {

const std::vector<std::string> view_names = {"1", "2", "3"};
const std::vector<Feature> features = {
    Feature(1, 1), Feature(2, 2), Feature(3, 3)};

TEST(Reconstruction, ViewIdFromNameValid) {
  Reconstruction reconstruction;
  const ViewId gt_view_id = reconstruction.AddView(view_names[0]);

  const ViewId view_id = reconstruction.ViewIdFromName(view_names[0]);
  EXPECT_EQ(gt_view_id, view_id);
}

TEST(Reconstruction, ViewIdFromNameInvalid) {
  Reconstruction reconstruction;
  EXPECT_EQ(reconstruction.ViewIdFromName(view_names[0]), kInvalidViewId);
}

TEST(Reconstruction, AddView) {
  Reconstruction reconstruction;
  const ViewId view_id = reconstruction.AddView(view_names[0]);
  EXPECT_NE(view_id, kInvalidViewId);
  EXPECT_EQ(reconstruction.NumViews(), 1);
  EXPECT_EQ(reconstruction.NumTracks(), 0);
  EXPECT_EQ(reconstruction.AddView(view_names[0]), kInvalidViewId);
  EXPECT_EQ(reconstruction.CameraIntrinsicsGroupIdFromViewId(view_id), 0);
}

TEST(Reconstruction, AddViewWithCameraIntrinsicsGroup) {
  Reconstruction reconstruction;
  const CameraIntrinsicsGroupId intrinsics_id = 1;
  const ViewId view_id = reconstruction.AddView(view_names[0], intrinsics_id);
  EXPECT_NE(view_id, kInvalidViewId);
  EXPECT_EQ(reconstruction.NumViews(), 1);
  EXPECT_EQ(reconstruction.NumTracks(), 0);
  EXPECT_EQ(reconstruction.NumCameraIntrinsicGroups(), 1);
  EXPECT_EQ(reconstruction.CameraIntrinsicsGroupIdFromViewId(view_id),
            intrinsics_id);
  EXPECT_EQ(reconstruction.AddView(view_names[0]), kInvalidViewId);
}

TEST(Reconstruction, RemoveView) {
  Reconstruction reconstruction;
  const ViewId view_id1 = reconstruction.AddView(view_names[0]);
  const ViewId view_id2 = reconstruction.AddView(view_names[1]);
  EXPECT_EQ(reconstruction.NumViews(), 2);
  EXPECT_EQ(reconstruction.NumCameraIntrinsicGroups(), 2);

  const CameraIntrinsicsGroupId view1_group =
      reconstruction.CameraIntrinsicsGroupIdFromViewId(view_id1);
  const CameraIntrinsicsGroupId view2_group =
      reconstruction.CameraIntrinsicsGroupIdFromViewId(view_id2);

  EXPECT_TRUE(reconstruction.RemoveView(view_id1));
  EXPECT_EQ(reconstruction.NumViews(), 1);
  EXPECT_EQ(reconstruction.ViewIdFromName(view_names[0]), kInvalidViewId);
  EXPECT_EQ(reconstruction.View(view_id1), nullptr);
  EXPECT_EQ(reconstruction.CameraIntrinsicsGroupIdFromViewId(view_id1),
            kInvalidCameraIntrinsicsGroupId);
  EXPECT_EQ(reconstruction.NumCameraIntrinsicGroups(), 1);
  const std::unordered_set<ViewId> view1_camera_intrinsics_group =
      reconstruction.GetViewsInCameraIntrinsicGroup(view1_group);
  EXPECT_FALSE(ContainsKey(view1_camera_intrinsics_group, view_id1));

  EXPECT_TRUE(reconstruction.RemoveView(view_id2));
  EXPECT_EQ(reconstruction.NumViews(), 0);
  EXPECT_EQ(reconstruction.ViewIdFromName(view_names[1]), kInvalidViewId);
  EXPECT_EQ(reconstruction.View(view_id2), nullptr);
  EXPECT_EQ(reconstruction.CameraIntrinsicsGroupIdFromViewId(view_id2),
            kInvalidCameraIntrinsicsGroupId);
  EXPECT_EQ(reconstruction.NumCameraIntrinsicGroups(), 0);
  const std::unordered_set<ViewId> view2_camera_intrinsics_group =
      reconstruction.GetViewsInCameraIntrinsicGroup(view2_group);
  EXPECT_FALSE(ContainsKey(view2_camera_intrinsics_group, view_id2));

  EXPECT_FALSE(reconstruction.RemoveView(kInvalidViewId));
  EXPECT_FALSE(reconstruction.RemoveView(view_id1));
}

TEST(Reconstruction, GetViewValid) {
  Reconstruction reconstruction;
  const ViewId view_id = reconstruction.AddView(view_names[0]);
  EXPECT_NE(view_id, kInvalidViewId);

  const View* const_view = reconstruction.View(view_id);
  EXPECT_NE(const_view, nullptr);

  View* mutable_view = reconstruction.MutableView(view_id);
  EXPECT_NE(mutable_view, nullptr);
}

TEST(Reconstruction, GetViewValidInvalid) {
  Reconstruction reconstruction;
  static const ViewId view_id = 0;
  const View* const_view = reconstruction.View(view_id);
  EXPECT_EQ(const_view, nullptr);

  View* mutable_view = reconstruction.MutableView(view_id);
  EXPECT_EQ(mutable_view, nullptr);
}

TEST(Reconstruction, GetViewsInCameraIntrinsicGroup) {
  static const double kFocalLength1 = 800.0;
  static const double kFocalLength2 = 1200.0;

  Reconstruction reconstruction;
  const ViewId view_id1 = reconstruction.AddView(view_names[0]);
  const CameraIntrinsicsGroupId intrinsics_id1 =
      reconstruction.CameraIntrinsicsGroupIdFromViewId(view_id1);

  // Add a second view with to the same camera intrinsics group.
  const ViewId view_id2 = reconstruction.AddView(view_names[1], intrinsics_id1);
  const CameraIntrinsicsGroupId intrinsics_id2 =
      reconstruction.CameraIntrinsicsGroupIdFromViewId(view_id2);
  EXPECT_EQ(intrinsics_id1, intrinsics_id2);

  // Add a third view that is in it's own camera intrinsics group.
  const ViewId view_id3 = reconstruction.AddView(view_names[2]);
  const CameraIntrinsicsGroupId intrinsics_id3 =
      reconstruction.CameraIntrinsicsGroupIdFromViewId(view_id3);
  EXPECT_NE(intrinsics_id1, intrinsics_id3);
  EXPECT_EQ(reconstruction.NumCameraIntrinsicGroups(), 2);

  // Change a value in view 1's camera intrinsics and ensure that it propagates
  // to view 2.
  Camera* camera1 = reconstruction.MutableView(view_id1)->MutableCamera();
  Camera* camera2 = reconstruction.MutableView(view_id2)->MutableCamera();
  Camera* camera3 = reconstruction.MutableView(view_id3)->MutableCamera();
  camera1->SetFocalLength(kFocalLength1);
  EXPECT_EQ(camera1->FocalLength(), camera2->FocalLength());
  EXPECT_NE(camera1->FocalLength(), camera3->FocalLength());

  // Alter the intrinsics through camera 2 and ensure that camera 1 is updated
  // and camera 3 is not.
  camera2->SetFocalLength(kFocalLength2);
  EXPECT_EQ(camera1->FocalLength(), camera2->FocalLength());
  EXPECT_NE(camera2->FocalLength(), camera3->FocalLength());
}

TEST(Reconstruction, CameraIntrinsicsGroupIds) {
  Reconstruction reconstruction;
  const ViewId view_id1 = reconstruction.AddView(view_names[0]);
  const CameraIntrinsicsGroupId intrinsics_id1 =
      reconstruction.CameraIntrinsicsGroupIdFromViewId(view_id1);

  // Add a second view with to the same camera intrinsics group.
  const ViewId view_id2 = reconstruction.AddView(view_names[1], intrinsics_id1);
  const CameraIntrinsicsGroupId intrinsics_id2 =
      reconstruction.CameraIntrinsicsGroupIdFromViewId(view_id2);
  EXPECT_EQ(intrinsics_id1, intrinsics_id2);

  // Add a third view that is in it's own camera intrinsics group.
  const ViewId view_id3 = reconstruction.AddView(view_names[2]);
  const CameraIntrinsicsGroupId intrinsics_id3 =
      reconstruction.CameraIntrinsicsGroupIdFromViewId(view_id3);
  EXPECT_NE(intrinsics_id1, intrinsics_id3);
  EXPECT_EQ(reconstruction.NumCameraIntrinsicGroups(), 2);

  // Ensure that the group ids are correct.
  const std::unordered_set<CameraIntrinsicsGroupId> group_ids =
      reconstruction.CameraIntrinsicsGroupIds();
  EXPECT_EQ(group_ids.size(), 2);
  EXPECT_TRUE(ContainsKey(group_ids, intrinsics_id1));
  EXPECT_TRUE(ContainsKey(group_ids, intrinsics_id3));
}

TEST(Reconstruction, AddEmptyTrack) {
  Reconstruction reconstruction;
  const TrackId track_id = reconstruction.AddTrack();
  EXPECT_NE(track_id, kInvalidTrackId);
}

TEST(Reconstruction, AddObservationValid) {
  Reconstruction reconstruction;

  const ViewId view_id1 = reconstruction.AddView(view_names[0]);
  const ViewId view_id2 = reconstruction.AddView(view_names[1]);
  EXPECT_NE(view_id1, kInvalidViewId);
  EXPECT_NE(view_id2, kInvalidViewId);

  const TrackId track_id = reconstruction.AddTrack();
  EXPECT_NE(track_id, kInvalidTrackId);

  EXPECT_TRUE(reconstruction.AddObservation(view_id1, track_id, features[0]));

  // Ensure that the observation adds the correct information to the view.
  const View* view1 = reconstruction.View(view_id1);
  const View* view2 = reconstruction.View(view_id2);
  EXPECT_EQ(view1->NumFeatures(), 1);
  EXPECT_EQ(view2->NumFeatures(), 0);

  const Feature* feature1 = view1->GetFeature(track_id);
  EXPECT_NE(feature1, nullptr);
  EXPECT_EQ(feature1->x(), features[0].x());
  EXPECT_EQ(feature1->y(), features[0].y());

  const Feature* feature2 = view2->GetFeature(track_id);
  EXPECT_EQ(feature2, nullptr);

  // Ensure that the observation adds the correct information to the track.
  const Track* track = reconstruction.Track(track_id);
  EXPECT_EQ(track->NumViews(), 1);
  EXPECT_TRUE(ContainsKey(track->ViewIds(), view_id1));
}

TEST(Reconstruction, AddObservationInvalid) {
  Reconstruction reconstruction;

  const ViewId view_id1 = reconstruction.AddView(view_names[0]);
  const ViewId view_id2 = reconstruction.AddView(view_names[1]);
  EXPECT_NE(view_id1, kInvalidViewId);
  EXPECT_NE(view_id2, kInvalidViewId);

  const TrackId track_id = reconstruction.AddTrack();
  EXPECT_NE(track_id, kInvalidTrackId);

  EXPECT_TRUE(reconstruction.AddObservation(view_id1, track_id, features[0]));
  EXPECT_TRUE(reconstruction.AddObservation(view_id2, track_id, features[0]));
  EXPECT_FALSE(reconstruction.AddObservation(view_id1, track_id, features[0]));
  EXPECT_FALSE(reconstruction.AddObservation(view_id2, track_id, features[0]));
  EXPECT_FALSE(reconstruction.AddObservation(view_id1, track_id, features[1]));
  EXPECT_FALSE(reconstruction.AddObservation(view_id2, track_id, features[1]));
}

TEST(Reconstruction, AddTrackValid) {
  Reconstruction reconstruction;

  const std::vector<std::pair<ViewId, Feature> > track = {{0, features[0]},
                                                          {1, features[1]}};
  EXPECT_NE(reconstruction.AddView(view_names[0]), kInvalidViewId);
  EXPECT_NE(reconstruction.AddView(view_names[1]), kInvalidViewId);

  const TrackId track_id = reconstruction.AddTrack(track);
  EXPECT_NE(track_id, kInvalidTrackId);
  EXPECT_TRUE(reconstruction.Track(track_id) != nullptr);
  EXPECT_EQ(reconstruction.NumTracks(), 1);
}

TEST(Reconstruction, AddTrackInvalid) {
  Reconstruction reconstruction;

  // Should fail with less than two views.
  const std::vector<std::pair<ViewId, Feature> > small_track = {
      {0, features[0]}};
  EXPECT_NE(reconstruction.AddView(view_names[0]), kInvalidViewId);
  EXPECT_EQ(reconstruction.AddTrack(small_track), kInvalidTrackId);
  EXPECT_EQ(reconstruction.NumTracks(), 0);
}

TEST(Reconstruction, RemoveTrackValid) {
  Reconstruction reconstruction;

  const std::vector<std::pair<ViewId, Feature> > track = {{0, features[0]},
                                                          {1, features[1]}};

  // Should be able to successfully remove the track.
  EXPECT_NE(reconstruction.AddView(view_names[0]), kInvalidViewId);
  EXPECT_NE(reconstruction.AddView(view_names[1]), kInvalidViewId);
  const TrackId track_id = reconstruction.AddTrack(track);
  EXPECT_TRUE(reconstruction.RemoveTrack(track_id));
}

TEST(Reconstruction, RemoveTrackInvalid) {
  Reconstruction reconstruction;

  // Should return false when trying to remove a track not in the
  // reconstruction.
  EXPECT_FALSE(reconstruction.RemoveTrack(kInvalidTrackId));
}

TEST(Reconstruction, GetTrackValid) {
  Reconstruction reconstruction;
  const std::vector<std::pair<ViewId, Feature> > track = {{0, features[0]},
                                                          {1, features[1]}};
  EXPECT_NE(reconstruction.AddView(view_names[0]), kInvalidViewId);
  EXPECT_NE(reconstruction.AddView(view_names[1]), kInvalidViewId);
  const TrackId track_id = reconstruction.AddTrack(track);
  EXPECT_NE(track_id, kInvalidTrackId);

  const Track* const_track = reconstruction.Track(track_id);
  EXPECT_NE(const_track, nullptr);

  Track* mutable_track = reconstruction.MutableTrack(track_id);
  EXPECT_NE(mutable_track, nullptr);
}

TEST(Reconstruction, GetTrackInvalid) {
  Reconstruction reconstruction;
  const std::vector<std::pair<ViewId, Feature> > track = {};
  const TrackId track_id = reconstruction.AddTrack(track);
  EXPECT_EQ(track_id, kInvalidTrackId);

  const Track* const_track = reconstruction.Track(track_id);
  EXPECT_EQ(const_track, nullptr);

  Track* mutable_track = reconstruction.MutableTrack(track_id);
  EXPECT_EQ(mutable_track, nullptr);
}

TEST(Reconstruction, GetSubReconstruction) {
  static const int kNumViews = 100;
  static const int kNumTracks = 1000;
  static const int kNumObservationsPerTrack = 10;

  Reconstruction reconstruction;
  for (int i = 0; i < kNumViews; i++) {
    const ViewId view_id = reconstruction.AddView(StringPrintf("%d", i));
    CHECK_NE(view_id, kInvalidViewId);
  }

  for (int i = 0; i < kNumTracks; i++) {
    std::vector<std::pair<ViewId, Feature> > track;
    for (int j = 0; j < kNumObservationsPerTrack; j++) {
      track.emplace_back((i + j) % kNumViews, Feature());
    }
    const TrackId track_id = reconstruction.AddTrack(track);
    CHECK_NE(track_id, kInvalidTrackId);
  }

  // Test subset extraction with a fixed subset size. We trivially take
  // consecutive view ids to choose the subset.
  static const int kNumViewsInSubset = 25;
  for (int i = 0; i < kNumViews - kNumViewsInSubset; i++) {
    std::unordered_set<ViewId> views_in_subset;
    for (int j = 0; j < kNumViewsInSubset; j++) {
      views_in_subset.emplace(i + j);
    }

    Reconstruction subset;
    reconstruction.GetSubReconstruction(views_in_subset, &subset);

    // Verify the subset by verifying that it contains only the specified views.
    EXPECT_EQ(subset.NumViews(), kNumViewsInSubset);
    const auto& view_ids = subset.ViewIds();
    // Verify that all views in the subset are in the reconstruction and in the
    // input views for the subset.
    for (const ViewId view_id : view_ids) {
      EXPECT_TRUE(ContainsKey(views_in_subset, view_id));

      // Ensure equality of the view objects.
      const View* view_in_reconstruction = reconstruction.View(view_id);
      const View* view_in_subset = subset.View(view_id);
      EXPECT_NE(view_in_reconstruction, nullptr);
      EXPECT_NE(view_in_subset, nullptr);
      EXPECT_EQ(view_in_reconstruction->IsEstimated(),
                view_in_subset->IsEstimated());
      // We only check the focal length in order to verify that the Camera
      // object was copied correctly.
      EXPECT_EQ(view_in_reconstruction->Camera().FocalLength(),
                view_in_subset->Camera().FocalLength());

      // Verify that the tracks exist in the subreconstruction and
      // reconstruction.
      const auto& tracks_in_view = view_in_subset->TrackIds();
      for (const TrackId track_id : tracks_in_view) {
        const Feature* feature_in_subset = view_in_subset->GetFeature(track_id);
        const Feature* feature_in_reconstruction =
            view_in_reconstruction->GetFeature(track_id);
        EXPECT_NE(feature_in_subset, nullptr);
        EXPECT_NE(feature_in_reconstruction, nullptr);
        EXPECT_EQ(feature_in_subset->x(), feature_in_reconstruction->x());
        EXPECT_EQ(feature_in_subset->y(), feature_in_reconstruction->y());
      }
    }

    // Verify that all tracks are valid.
    const auto& track_ids = subset.TrackIds();
    for (const TrackId track_id : track_ids) {
      const Track* track_in_reconstruction = reconstruction.Track(track_id);
      const Track* track_in_subset = subset.Track(track_id);
      EXPECT_NE(track_in_reconstruction, nullptr);
      EXPECT_NE(track_in_subset, nullptr);
      EXPECT_EQ(
          (track_in_subset->Point() - track_in_reconstruction->Point()).norm(),
          0.0);

      // Ensure that all views observing the subset's track are actually in the
      // subset.
      const auto& views_observing_track = track_in_subset->ViewIds();
      for (const ViewId view_id : views_observing_track) {
        EXPECT_TRUE(ContainsKey(views_in_subset, view_id));
      }
    }

    // Ensure that RemoveView works properly.
    for (const ViewId view_id : views_in_subset) {
      ASSERT_TRUE(subset.RemoveView(view_id));
    }
  }
}

}  // namespace theia
