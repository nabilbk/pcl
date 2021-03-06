/*
 * Software License Agreement (BSD License)
 *
 *  Point Cloud Library (PCL) - www.pointclouds.org
 *  Copyright (c) 2012-, Open Perception, Inc.
 *
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the copyright holder(s) nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */
#include <pcl/stereo/digital_elevation_map.h>

#include <algorithm>

template class PCL_EXPORTS pcl::DigitalElevationMapBuilder<pcl::PointDEM>;

// Type for histograms.
  class FeatureHistogram
  {
    public:
      // Public constructor.
      FeatureHistogram (size_t number_of_bins);
      // Public destructor.
      virtual ~FeatureHistogram ();

      // Set min and max thresholds.
      void
      setThresholds(float min, float max);

      // Get the lower threshold.
      float
      getThresholdMin() const;

      // Get the upper threshold.
      float
      getThresholdMax() const;

      // Get number of elements was added to the histogram.
      size_t
      getNumberOfElements() const;

      // Get number of bins in the histogram.
      size_t
      getNumberOfBins() const;

      // Increase a bin, that corresponds the value.
      void
      addValue (float value);

      // Get value, corresponds to the greatest bin.
      float
      meanValue ();

      // Get variance of the value.
      float
      variance (float mean);

    protected:
      // Vector, that contain the histogram.
      std::vector <unsigned> histogram_;

      // Thresholds.
      float threshold_min_;
      float threshold_max_;
      // "Width" of a bin.
      float step_;

      // Number of values was added to the histogram.
      size_t number_of_elements_;

      // Number of bins.
      size_t number_of_bins_;
  };

// DigitalElevationMapBuilder class implementation //////////////////////

template <typename PointT>
pcl::DigitalElevationMapBuilder<typename PointT>::DigitalElevationMapBuilder ()
{
  resolution_column_ = 64;
  resolution_disparity_ = 32;
  min_points_in_cell_ = 1;
}

template <typename PointT>
pcl::DigitalElevationMapBuilder<typename PointT>::~DigitalElevationMapBuilder ()
{
  
}

template <typename PointT> void
pcl::DigitalElevationMapBuilder<PointT>::setResolution (size_t resolution_column, size_t resolution_disparity)
{
  resolution_column_ = resolution_column;
  resolution_disparity_ = resolution_disparity;
}

template <typename PointT> size_t
pcl::DigitalElevationMapBuilder<PointT>::getColumnResolution () const
{
  return resolution_column_;
}

template <typename PointT> size_t
pcl::DigitalElevationMapBuilder<PointT>::getDisparityResolution () const
{
  return resolution_disparity_;
}

template <typename PointT> void 
pcl::DigitalElevationMapBuilder<PointT>::setMinPointsInCell(size_t min_points_in_cell)
{
  min_points_in_cell_ = min_points_in_cell;
}

template <typename PointT> size_t
pcl::DigitalElevationMapBuilder<PointT>::getMinPointsInCell() const
{
  return min_points_in_cell_;
}

// Build DEM.
template <typename PointT> void 
pcl::DigitalElevationMapBuilder<PointT>::compute (PointCloudPointer &out_cloud)
{
  // Initialize.
  // Initialize the output cloud.
  out_cloud = PointCloudPointer (new pcl::PointCloud<PointT>);
  out_cloud->width = resolution_column_;
  out_cloud->height = resolution_disparity_;
  out_cloud->resize (out_cloud->width * out_cloud->height);

  // Initialize steps.
  const size_t kColumnStep = (disparity_map_width_ - 1) / resolution_column_ + 1;
  const float kDisparityStep = (disparity_threshold_max_ - disparity_threshold_min_) / 
      resolution_disparity_;

  // Initialize histograms.
  const size_t kNumberOfHistograms = resolution_column_ * resolution_disparity_;

  const float kHeightMin = -0.5f;
  const float kHeightMax = 1.5f;
  const float kHeightResolution = 0.01f;
  const size_t kHeightBins = static_cast<size_t> ((kHeightMax - kHeightMin) / kHeightResolution);
  // Histogram for initializing other height histograms.
  FeatureHistogram height_histogram_example (kHeightBins);
  height_histogram_example.setThresholds (kHeightMin, kHeightMax);

  const float kIntensityMin = 0.0f;
  const float kIntensityMax = 255.0f;
  const size_t kIntensityBins = 256;
  // Histogram for initializing other intensity histograms.
  FeatureHistogram intensity_histogram_example (kIntensityBins);
  intensity_histogram_example.setThresholds (kIntensityMin, kIntensityMax);

  std::vector <FeatureHistogram> height_histograms 
      (kNumberOfHistograms, height_histogram_example);
  std::vector <FeatureHistogram> intensity_histograms 
      (kNumberOfHistograms, intensity_histogram_example);

  // Check, if an image was loaded.
  if (!image_)
  {
    PCL_ERROR ("[pcl::DisparityMapConverter::compute] Memory for the image was not allocated.\n");
    return;
  }

  for(size_t column = 0; column < disparity_map_width_; ++column)
  {
    for(size_t row = 0; row < disparity_map_height_; ++row)
    {
      float disparity = disparity_map_[column + row * disparity_map_width_];
      if (disparity_threshold_min_ < disparity && disparity < disparity_threshold_max_)
      {
        // Find a height and an intensity of the point of interest.
        PointXYZ point_3D = translateCoordinates (row, column, disparity);
        float height = point_3D.y;

        RGB point_RGB = image_->points[column + row * disparity_map_width_];
        float intensity = static_cast<float> ((point_RGB.r + point_RGB.g + point_RGB.b) / 3);

        // Calculate index of histograms.
        size_t index_column = column / kColumnStep;
        size_t index_disparity = static_cast<size_t>(
            (disparity - disparity_threshold_min_) / kDisparityStep);

        size_t index = index_column + index_disparity * resolution_column_;

        // Increase the histograms.
        height_histograms[index].addValue (height);
        intensity_histograms[index].addValue (intensity);

      } // if
    } // row
  } // column
  
  // For all gistograms.
  for (size_t index_column = 0; index_column < resolution_column_; ++index_column)
  {
    for (size_t index_disparity = 0; index_disparity < resolution_disparity_; ++index_disparity)
    {
      size_t index = index_column + index_disparity * resolution_column_;
      // Compute the corresponding DEM cell.
      size_t column = index_column * kColumnStep;
      float disparity = disparity_threshold_min_ +
          static_cast<float> (index_disparity) * kDisparityStep;
      
      PointXYZ point_3D = translateCoordinates (0, column, disparity);
      PointDEM point_DEM;
      point_DEM.x = point_3D.x;
      point_DEM.z = point_3D.z;

      if (height_histograms[index].getNumberOfElements () >= min_points_in_cell_)
      {
        point_DEM.y = height_histograms[index].meanValue ();
        point_DEM.height_variance = height_histograms[index].variance (point_DEM.y);

        point_DEM.intensity = intensity_histograms[index].meanValue ();
        point_DEM.intensity_variance = intensity_histograms[index].variance (point_DEM.intensity);
      }
      else // height_histograms[index].getNumberOfElements () < min_points_in_cell_
      {
        point_DEM.y = 0.0f;
        point_DEM.intensity = 255.0f;
        // Set variances to -1, it means that point is invalid.
        point_DEM.height_variance = -1.0f;
        point_DEM.intensity_variance = -1.0f;
      }

      out_cloud->at(index_column, index_disparity) = point_DEM;
      
    } // index_disparity
  } // index_column
}

// FeatureHistogram class implementation //////////////////////

FeatureHistogram::FeatureHistogram (size_t number_of_bins) : 
    histogram_ (number_of_bins, 0)
{
  // Initialize thresholds.
  threshold_min_ = 0.0f;
  threshold_max_ = static_cast<float> (number_of_bins);
  step_ = 1.0f;

  // Initialize sum.
  number_of_elements_ = 0;

  // Initialize size;
  number_of_bins_ = number_of_bins;
}

FeatureHistogram::~FeatureHistogram ()
{
  
}

void
FeatureHistogram::setThresholds(float min, float max)
{
  if (min < max)
  {
    threshold_min_ = min;
    threshold_max_ = max;
    step_ = (max - min) / static_cast<float> (number_of_bins_);
  }
  else
  {
    PCL_ERROR ("[FeatureHistogram::setThresholds] Variable \"max\" must be greater then \"min\".\n");
    return;
  }
}

float
FeatureHistogram::getThresholdMin() const
{
  return threshold_min_;
}

float
FeatureHistogram::getThresholdMax() const
{
  return threshold_max_;
}

size_t
FeatureHistogram::getNumberOfElements() const
{
  return number_of_elements_;
}

size_t
FeatureHistogram::getNumberOfBins() const
{
  return number_of_bins_;
}

void
FeatureHistogram::addValue (float value)
{
  // Check, if value in the allowed range.
  if (threshold_min_ < value && value < threshold_max_)
  {
    // Increase the sum.
    ++number_of_elements_;

    // Increase the bin.
    size_t bin_number = static_cast<size_t> ((value - threshold_min_) / step_);
    ++histogram_[bin_number];
  }
}

float
FeatureHistogram::meanValue ()
{
  std::vector <unsigned>::iterator begin = histogram_.begin ();
  // Find a bin with maximum value.
  std::vector <unsigned>::iterator max_iterator = std::max_element (begin, histogram_.end ());

  // Find index of the maximum bin.
  size_t max_index = max_iterator - begin;

  // Compute mean value.
  float mean = step_ * static_cast<float> (max_index) + threshold_min_;

  return mean;
}

float
FeatureHistogram::variance (float mean)
{
  // Check, if the histogram is empty.
  if (number_of_elements_ == 0)
  {
    return -1.0f;
  }
  // The histogram is not empty.
  // Variable to accumulate the terms of variance.
  float variances_sum = 0;

  for (size_t bin = 0; bin < number_of_bins_; ++bin)
  {
    if (histogram_[bin] > 0)
    {
      // Value corresponding to the bin.
      float value = step_ * static_cast<float> (bin) + threshold_min_;
      float dif = static_cast<float> (histogram_[bin]) * value - mean;
      variances_sum += dif * dif;
    }
  }

  // Compute variance and return it.
  return variances_sum / static_cast<float> (number_of_elements_);
}