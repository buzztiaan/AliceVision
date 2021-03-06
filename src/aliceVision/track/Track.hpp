// This file is part of the AliceVision project.
// Copyright (c) 2016 AliceVision contributors.
// Copyright (c) 2012 openMVG contributors.
// This Source Code Form is subject to the terms of the Mozilla Public License,
// v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <aliceVision/config.hpp>
#include <aliceVision/feature/imageDescriberCommon.hpp>
#include <aliceVision/matching/IndMatch.hpp>
#include <aliceVision/stl/FlatMap.hpp>
#include <aliceVision/stl/FlatSet.hpp>

#include <lemon/list_graph.h>
#include <lemon/unionfind.h>

#include <algorithm>
#include <iostream>
#include <functional>
#include <vector>
#include <set>
#include <map>
#include <memory>

namespace aliceVision {
namespace track {

using namespace aliceVision::matching;
using namespace lemon;

/**
 * @brief A Track is a feature visible accross multiple views.
 * Tracks are generated by the fusion of all matches accross all images.
 */
struct Track
{
  /// Data structure to store a track: collection of {ViewId, FeatureId}
  typedef stl::flat_map<std::size_t, std::size_t> FeatureIdPerView;

  Track() {}

  /// Descriptor type
  feature::EImageDescriberType descType = feature::EImageDescriberType::UNINITIALIZED;
  /// Collection of matched features between views: {ViewId, FeatureId}
  FeatureIdPerView featPerView;
};

/// A track is a collection of {trackId, Track}
typedef stl::flat_map<std::size_t, Track> TracksMap;
typedef std::vector<std::size_t> TrackIdSet;

/**
 * @brief Data structure that contains for each features of each view, its corresponding cell positions for each level of the pyramid, i.e.
 * for each view:
 *   each feature is mapped N times (N=depth of the pyramid)
 *      each times it contains the absolute position P of the cell in the corresponding pyramid level
 *
 * FeatsPyramidPerView contains map<viewId, map<trackId*N, pyramidIndex>>
 *
 * Cell position:
 * Consider the set of all cells of all pyramids, there are M = \sum_{l=1...N} K_l^2 cells with K_l = 2^l and l=1...N
 * We enumerate the cells starting from the first pyramid l=1 (so that they have position from 0 to 3 (ie K^2 - 1))
 * and we go on for increasing values of l so that e.g. the first cell of the pyramid at l=2 has position K^2, the second K^2 + 1 etc...
 * So in general the i-th cell of the pyramid at level l has position P= \sum_{j=1...l-1} K_j^2 + i
 */
typedef stl::flat_map<std::size_t, stl::flat_map<std::size_t, std::size_t> > TracksPyramidPerView;

/**
 * @brief TracksPerView is a list of visible track ids for each view.
 * TracksPerView contains <viewId, vector<trackId>>
 */
typedef stl::flat_map<std::size_t, TrackIdSet > TracksPerView;

/**
 * @brief KeypointId is a unique ID for a feature in a view.
 */
struct KeypointId
{
  KeypointId(){}
  KeypointId(feature::EImageDescriberType type, std::size_t index)
    : descType(type)
    , featIndex(index)
  {}

  bool operator<(const KeypointId& other) const
  {
    if(descType == other.descType)
      return featIndex < other.featIndex;
    return descType < other.descType;
  }

  feature::EImageDescriberType descType = feature::EImageDescriberType::UNINITIALIZED;
  std::size_t featIndex = 0;
};

inline std::ostream& operator<<(std::ostream& os, const KeypointId& k)
{
    os << feature::EImageDescriberType_enumToString(k.descType) << ", " << k.featIndex;
    return os;
}

/**
 * @brief Allows to create Tracks from a set of Matches accross Views.
 *
 * Implementation of [1] an efficient algorithm to compute track from pairwise
 * correspondences.
 *
 * [1] "Unordered feature tracking made fast and easy"
 *     Pierre Moulon and Pascal Monasse. CVMP 2012
 *
 * It tracks the position of features along the series of image from pairwise
 *  correspondences.
 *
 * From map< [imageI,ImageJ], [indexed matches array] > it builds tracks.
 *
 * Usage:
 * @code{.cpp}
 *  PairWiseMatches matches;
 *  PairedIndMatchImport(matchFile, matches); // load series of pairwise matches
 *  // compute tracks from matches
 *  TracksBuilder tracksBuilder;
 *  track::Tracks tracks;
 *  tracksBuilder.build(matches); // build: Efficient fusion of correspondences
 *  tracksBuilder.filter(true, 2);           // filter: Remove track that have conflict
 *  tracksBuilder.exportToSTL(tracks); // build tracks with STL compliant type
 * @endcode
 */
struct TracksBuilder
{
  /// IndexedFeaturePair is: map<viewId, keypointId>
  typedef std::pair<std::size_t, KeypointId> IndexedFeaturePair;
  typedef ListDigraph::NodeMap<std::size_t> IndexMap;
  typedef lemon::UnionFindEnum< IndexMap > UnionFindObject;

  typedef stl::flat_map< lemon::ListDigraph::Node, IndexedFeaturePair> MapNodeToIndex;
  typedef stl::flat_map< IndexedFeaturePair, lemon::ListDigraph::Node > MapIndexToNode;

  /// graph container to create the node
  lemon::ListDigraph _graph;
  /// node to index map
  MapNodeToIndex _map_nodeToIndex;
  std::unique_ptr<IndexMap> _index;
  std::unique_ptr<UnionFindObject> _tracksUF;

  const UnionFindObject& getUnionFindEnum() const
  {
    return *_tracksUF;
  }

  const MapNodeToIndex& getReverseMap() const
  {
    return _map_nodeToIndex;
  }

  /**
   * @brief Build tracks for a given series of pairWise matches
   * @param[in] pairwiseMatches PairWise matches
   */
  void build(const PairwiseMatches& pairwiseMatches);

  /**
   * @brief Remove bad tracks (too short or track with ids collision)
   * @param[in] clearForks: remove tracks with multiple observation in a single image
   * @param[in] minTrackLength: minimal number of observations to keep the track
   * @param[in] multithreaded Is multithreaded
   */
  void filter(bool clearForks, std::size_t minTrackLength, bool multithreaded = true);

  /**
   * @brief Export to stream
   * @param[out] os stream
   * @return
   */
  bool exportToStream(std::ostream& os);

  /**
   * @brief Export tracks as a map (each entry is a sequence of imageId and keypointId):
   *        {TrackIndex => {(imageIndex, keypointId), ... ,(imageIndex, keypointId)}
   */
  void exportToSTL(TracksMap& allTracks) const;

  /**
   * @brief Return the number of connected set in the UnionFind structure (tree forest)
   * @return number of connected set in the UnionFind structure
   */
  std::size_t nbTracks() const
  {
    std::size_t cpt = 0;
    for(lemon::UnionFindEnum< IndexMap >::ClassIt cit(*_tracksUF); cit != INVALID; ++cit)
      ++cpt;
    return cpt;
  }
};

namespace tracksUtilsMap {

/**
 * @brief Find common tracks between images.
 * @param[in] imageIndexes: set of images we are looking for common tracks
 * @param[in] tracksIn: all tracks of the scene
 * @param[out] tracksOut: output with only the common tracks
 */
bool getCommonTracksInImages(const std::set<std::size_t>& imageIndexes,
                                    const TracksMap& tracksIn,
                                    TracksMap & tracksOut);
  
/**
 * @brief Find common tracks among a set of images.
 * @param[in] imageIndexes: set of images we are looking for common tracks.
 * @param[in] tracksPerView: for each view it contains the list of visible tracks. *The tracks ids must be ordered*.
 * @param[out] visibleTracks: output with only the common tracks.
 */
void getCommonTracksInImages(const std::set<std::size_t>& imageIndexes,
                                    const TracksPerView& tracksPerView,
                                    std::set<std::size_t>& visibleTracks);
  
/**
 * @brief Find common tracks among images.
 * @param[in] imageIndexes: set of images we are looking for common tracks.
 * @param[in] tracksIn: all tracks of the scene.
 * @param[in] tracksPerView: for each view the id of the visible tracks.
 * @param[out] tracksOut: output with only the common tracks.
 */
bool getCommonTracksInImagesFast(const std::set<std::size_t>& imageIndexes,
                                          const TracksMap& tracksIn,
                                          const TracksPerView& tracksPerView,
                                          TracksMap& tracksOut);
  
/**
 * @brief Find all the visible tracks from a set of images.
 * @param[in] imagesId set of images we are looking for tracks.
 * @param[in] tracks all tracks of the scene.
 * @param[out] tracksId the tracks in the images
 */
void getTracksInImages(const std::set<std::size_t>& imagesId,
                              const TracksMap& tracks,
                              std::set<std::size_t>& tracksId);

/**
 * @brief Find all the visible tracks from a set of images.
 * @param[in] imagesId set of images we are looking for tracks.
 * @param[in] tracksPerView for each view the id of the visible tracks.
 * @param[out] tracksId the tracks in the images
 */
void getTracksInImagesFast(const std::set<IndexT>& imagesId,
                                  const TracksPerView& tracksPerView,
                                  std::set<IndexT>& tracksIds);

/**
 * @brief Find all the visible tracks from a single image.
 * @param[in] imageIndex of the image we are looking for tracks.
 * @param[in] tracks all tracks of the scene.
 * @param[out] tracksIds the tracks in the image
 */
inline void getTracksInImage(const std::size_t& imageIndex,
                             const TracksMap& tracks,
                             std::set<std::size_t>& tracksIds)
{
  tracksIds.clear();
  for(auto& track: tracks)
  {
    const auto iterSearch = track.second.featPerView.find(imageIndex);
    if(iterSearch != track.second.featPerView.end())
      tracksIds.insert(track.first);
  }
}

/**
 * @brief Find all the visible tracks from a set of images.
 * @param[in] imageId of the image we are looking for tracks.
 * @param[in] map_tracksPerView for each view the id of the visible tracks.
 * @param[out] tracksIds the tracks in the images
 */
inline void getTracksInImageFast(const std::size_t& imageId,
                                 const TracksPerView& tracksPerView,
                                 std::set<std::size_t>& tracksIds)
{
  if(tracksPerView.find(imageId) == tracksPerView.end())
    return;

  const TrackIdSet& imageTracks = tracksPerView.at(imageId);
  tracksIds.clear();
  tracksIds.insert(imageTracks.cbegin(), imageTracks.cend());
}

/**
 * @brief computeTracksPerView
 * @param[in] tracks
 * @param[out] tracksPerView
 */
void computeTracksPerView(const TracksMap& tracks, TracksPerView& tracksPerView);

/**
 * @brief Return the tracksId as a set (sorted increasing)
 * @param[in] tracks
 * @param[out] tracksIds
 */
inline void getTracksIdVector(const TracksMap& tracks,
                              std::set<std::size_t>* tracksIds)
{
  tracksIds->clear();
  for (TracksMap::const_iterator iterT = tracks.begin(); iterT != tracks.end(); ++iterT)
    tracksIds->insert(iterT->first);
}

using FeatureId = std::pair<feature::EImageDescriberType, std::size_t>;

/**
 * @brief Get feature id (with associated describer type) in the specified view for each TrackId
 * @param[in] allTracks
 * @param[in] trackIds
 * @param[in] viewId
 * @param[out] out_featId
 * @return
 */
inline bool getFeatureIdInViewPerTrack(const TracksMap& allTracks,
                                       const std::set<std::size_t>& trackIds,
                                       IndexT viewId,
                                       std::vector<FeatureId>* out_featId)
{
  for(std::size_t trackId: trackIds)
  {
    TracksMap::const_iterator iterT = allTracks.find(trackId);

    // ignore it if the track doesn't exist
    if(iterT == allTracks.end())
      continue;

    // try to find imageIndex
    const Track& map_ref = iterT->second;
    auto iterSearch = map_ref.featPerView.find(viewId);
    if(iterSearch != map_ref.featPerView.end())
      out_featId->emplace_back(map_ref.descType, iterSearch->second);
  }
  return !out_featId->empty();
}

struct FunctorMapFirstEqual : public std::unary_function <TracksMap , bool>
{
  std::size_t id;
  FunctorMapFirstEqual(std::size_t val):id(val){};
  bool operator()(const std::pair<std::size_t, Track > & val) {
    return ( id == val.first);
  }
};

/**
 * @brief Convert a trackId to a vector of indexed Matches.
 *
 * @param[in]  map_tracks: set of tracks with only 2 elements
 *             (image A and image B) in each Track.
 * @param[in]  vec_filterIndex: the track indexes to retrieve.
 *             Only track indexes contained in this filter vector are kept.
 * @param[out] pvec_index: list of matches
 *             (feature index in image A, feature index in image B).
 *
 * @warning The input tracks must be composed of only two images index.
 * @warning Image index are considered sorted (increasing order).
 */
inline void tracksToIndexedMatches(const TracksMap& tracks,
                                   const std::vector<IndexT>& filterIndex,
                                   std::vector<IndMatch>* out_index)
{

  std::vector<IndMatch>& vec_indexref = *out_index;
  vec_indexref.clear();

  for(std::size_t i = 0; i < filterIndex.size(); ++i)
  {
    // retrieve the track information from the current index i.
    TracksMap::const_iterator itF = std::find_if(tracks.begin(), tracks.end(), FunctorMapFirstEqual(filterIndex[i]));

    // the current track.
    const Track& map_ref = itF->second;

    // check we have 2 elements for a track.
    assert(map_ref.featPerView.size() == 2);

    const IndexT indexI = (map_ref.featPerView.begin())->second;
    const IndexT indexJ = (++map_ref.featPerView.begin())->second;

    vec_indexref.emplace_back(indexI, indexJ);
  }
}

/**
 * @brief Return the occurrence of tracks length.
 * @param[in] tracks
 * @param[out] occurenceTrackLength
 */
inline void tracksLength(const TracksMap& tracks,
                         std::map<std::size_t, std::size_t>& occurenceTrackLength)
{
  for(TracksMap::const_iterator iterT = tracks.begin(); iterT != tracks.end(); ++iterT)
  {
    const std::size_t trLength = iterT->second.featPerView.size();

    if(occurenceTrackLength.end() == occurenceTrackLength.find(trLength))
      occurenceTrackLength[trLength] = 1;
    else
      occurenceTrackLength[trLength] += 1;
  }
}

/**
 * @brief Return a set containing the image Id considered in the tracks container.
 * @param[in] tracksPerView
 * @param[out] imagesId
 */
inline void imageIdInTracks(const TracksPerView& tracksPerView,
                            std::set<std::size_t>& imagesId)
{
  for(const auto& viewTracks: tracksPerView)
    imagesId.insert(viewTracks.first);
}

/**
 * @brief Return a set containing the image Id considered in the tracks container.
 * @param[in] tracks
 * @param[out] imagesId
 */
inline void imageIdInTracks(const TracksMap& tracks,
                            std::set<std::size_t>& imagesId)
{
  for (TracksMap::const_iterator iterT = tracks.begin(); iterT != tracks.end(); ++iterT)
  {
    const Track& map_ref = iterT->second;
    for(auto iter = map_ref.featPerView.begin(); iter != map_ref.featPerView.end(); ++iter)
      imagesId.insert(iter->first);
  }
}

} // namespace tracksUtilsMap
} // namespace track
} // namespace aliceVision
