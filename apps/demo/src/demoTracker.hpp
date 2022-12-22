#pragma once

#include <opencv2/videoio.hpp>

#include "tracker.hpp"

inline double bbox_overlap(const cv::Rect& bbox1, const cv::Rect& bbox2);
inline bool bbox1_contain_bbox2(const cv::Rect& bbox1, const cv::Rect& bbox2);

using namespace sky360lib::tracking;

/*########################################################################################################################
# This class represents a single target/blob that has been identified on the frame and is currently being tracked      #
# If track validation is turned on then there are a couple of further steps that are taken in order to ensure the      #
# target is valid and not a phantom target. This logic is by no means perfect or bullet proof but as tracking an       #
# object is the most expensive in terms of processing time, we need only try and track targets that have the potential # 
# to be good and valid targets.                                                                                        #
########################################################################################################################*/
class DemoTracker
{
public:
    enum TargetStatus
    {
        PROVISIONARY_TARGET = 1,
        ACTIVE_TARGET = 2,
        LOST_TARGET = 3
    };
    struct Track
    {
        Track(cv::Point2i _center, cv::Scalar _color)
            : center(_center), color(_color)
        {}

        cv::Point2i center;
        cv::Scalar color;
    };

    DemoTracker(int id, const cv::Rect& bbox, const cv::Mat& frame, 
        bool track_plotting_enabled = true, bool track_prediction_enabled = false, bool enable_track_validation = true,
        int stationary_track_threshold = 5, int orphaned_track_thold = 20)
    {
        m_id = id;
        m_track_plotting_enabled = track_plotting_enabled;
        m_track_prediction_enabled = track_prediction_enabled;
        m_enable_track_validation = enable_track_validation;
        m_stationary_track_threshold = stationary_track_threshold;
        m_orphaned_track_thold = orphaned_track_thold;

        m_bboxes.push_back(bbox);
        m_tracking_state = TargetStatus::PROVISIONARY_TARGET;
        m_tracker = TrackerCSRT::create();
        m_tracker->init(frame, bbox);
        m_stationary_track_counter = 0;
        m_active_track_counter = 0;
        m_bbox_to_check = bbox;
        m_start = getAbsoluteTime();
        m_second_counter = 0;
        m_tracked_boxes.push_back(bbox);
        //self.track_predictor = TrackPrediction(id, bbox)
    }

    // function to get the latest bbox in the format (x1,y1,w,h)
    const cv::Rect& get_bbox()
    {
        return m_bboxes.back();
    }

    // function to determine the center of the the bbox being tracked
    const cv::Point2i get_center()
    {
        const cv::Rect& rect = get_bbox();
        return cv::Point2i(rect.x + (rect.width / 2), rect.y + (rect.height / 2));
    }

    // function to update the bbox on the frame, also if validation is enabled then some addtional logic is executed to determine 
    // if the target is still a avlid target
    bool update(const cv::Mat& frame, cv::Rect& bbox)
    {
        bool success = m_tracker->update(frame, bbox);
        if (success)
        {
            m_bboxes.push_back(bbox);

            // Mike: If we have track plotting enabled, then we need to store the center points of the bboxes so that we can plo the 
            // entire track on the frame including the colour
            if (m_track_plotting_enabled)
                m_center_points.push_back(Track(get_center(), bbox_color()));

            // TODO
            // if (m_track_prediction_enabled)
            //     m_predictor_center_points.push_back(self.track_predictor.update(bbox))

            if (m_enable_track_validation)
            {
                // Mike: perform validation logic every second, on the tickover of that second. Validation logic is very much dependent on the 
                // target moving a certain amount over time. The technology that we use does have its limitation in that it will 
                // identify and try and track false positives. This validaiton logic is in place to try and limit this
                bool validate_bbox = false;

                if ((int)(getAbsoluteTime() - m_start) > m_second_counter)
                {
                    m_tracked_boxes.push_back(bbox);
                    ++m_second_counter;
                    validate_bbox = true;
                }

                // Mike: grab the validation config options from the settings dictionary
                int stationary_scavanage_threshold = (int)(m_stationary_track_threshold * 1.5);

                // Mike: Only process validation after a second, we need to allow the target to move
                if (m_tracked_boxes.size() > 1)
                {
                    // Mike: if the item being tracked has moved out of its initial bounds, then it's an active target
                    if (bbox_overlap(m_bbox_to_check, bbox) == 0.0)
                    {
                        if (m_tracking_state != TargetStatus::ACTIVE_TARGET)
                        {
                            m_tracking_state = TargetStatus::ACTIVE_TARGET;
                            m_bbox_to_check = bbox;
                            m_stationary_track_counter = 0;
                        }
                    }
                    if (validate_bbox)
                    {
                        if (bbox_overlap(m_bbox_to_check, m_tracked_boxes.back()) > 0)
                        {
                            // Mike: this bounding box has remained pretty static, its now closer to getting scavenged
                            ++m_stationary_track_counter;
                        }
                        else
                        {
                            m_stationary_track_counter = 0;
                        }
                    }
                }
                // Mike: If the target has not moved for a period of time, we classify the target as lost
                if (m_stationary_track_threshold <= m_stationary_track_counter && 
                    m_stationary_track_counter < stationary_scavanage_threshold)
                {
                    // Mike: If its not moved enough then mark it as red for potential scavenging
                    m_tracking_state = TargetStatus::LOST_TARGET;
                }
                else if (m_stationary_track_counter >= stationary_scavanage_threshold)
                {
                    // If it has remained stationary for a period of time then we are no longer interested
                    success = false;
                }
                // Mike: If its an active target then update counters at the point of validation
                if (m_tracking_state == TargetStatus::ACTIVE_TARGET)
                {
                    ++m_active_track_counter;
                    if (m_active_track_counter > m_orphaned_track_thold)
                    {
                        m_bbox_to_check = bbox;
                        m_active_track_counter = 0;
                    }
                }
            }
        }
        return success;
    }

    const cv::Scalar bbox_color()
    {
        switch (m_tracking_state)
        {
            case TargetStatus::PROVISIONARY_TARGET: return cv::Scalar(50, 50, 225);
            case TargetStatus::ACTIVE_TARGET: return cv::Scalar(50, 50, 225);
            case TargetStatus::LOST_TARGET: return cv::Scalar(50, 50, 225);
        }
        return cv::Scalar(255, 255, 225);
    }

    bool is_tracking()
    {
        return m_tracking_state == TargetStatus::ACTIVE_TARGET;
    }

    bool does_bbx_overlap(const cv::Rect& bbox)
    {
        return bbox_overlap(m_bboxes.back(), bbox) > 0;
    }

    bool is_bbx_contained(const cv::Rect& bbox)
    {
        return bbox1_contain_bbox2(m_bboxes.back(), bbox);
    }

private:
    int m_id;
    TargetStatus m_tracking_state;
    std::vector<cv::Rect> m_bboxes;
    cv::Ptr<TrackerCSRT> m_tracker = nullptr;
    int m_stationary_track_counter;
    int m_active_track_counter;
    cv::Rect m_bbox_to_check;
    int m_second_counter;
    std::vector<cv::Rect> m_tracked_boxes;
    std::vector<Track> m_center_points;
    //std::vector<cv::Point2i> predictor_center_points;
    double m_start;
    bool m_track_plotting_enabled = true;
    bool m_track_prediction_enabled = false;
    bool m_enable_track_validation = true;
    int m_stationary_track_threshold;
    int m_orphaned_track_thold;
};


inline double bbox_overlap(const cv::Rect& bbox1, const cv::Rect& bbox2)
{
    // determine the coordinates of the intersection rectangle
    int x_left = std::max(bbox1.x, bbox2.x);
    int y_top = std::max(bbox1.y, bbox2.y);
    int x_right = std::min(bbox1.x + bbox1.width, bbox2.x + bbox2.width);
    int y_bottom = std::min(bbox1.y + bbox1.height, bbox2.y + bbox2.height);

    if (x_right < x_left || y_bottom < y_top)
        return 0.0;

    // The intersection of two axis-aligned bounding boxes is always an
    // axis-aligned bounding box.
    // NOTE: We MUST ALWAYS add +1 to calculate area when working in
    // screen coordinates, since 0,0 is the top left pixel, and w-1,h-1
    // is the bottom right pixel. If we DON'T add +1, the result is wrong.
    int intersection_area = (x_right - x_left + 1) * (y_bottom - y_top + 1);

    // compute the area of both AABBs
    int bb1_area = (bbox1.width + 1) * (bbox1.height + 1);
    int bb2_area = (bbox2.width + 1) * (bbox2.height + 1);

    // compute the intersection over union by taking the intersection
    // area and dividing it by the sum of prediction + ground-truth areas - the interesection area
    return (double)intersection_area / (double)(bb1_area + bb2_area - intersection_area);
}

// Utility function to determine if a bounding box 1 contains bounding box 2. In order to make tracking more efficient
// we try not to track sections of the same point of interest (blob)
inline bool bbox1_contain_bbox2(const cv::Rect& bbox1, const cv::Rect& bbox2)
{
    return (bbox2.x > bbox1.x) && 
        (bbox2.y > bbox1.y) && 
        ((bbox2.x + bbox2.width) < (bbox1.x + bbox1.width)) && 
        ((bbox2.y + bbox2.height) < (bbox1.y + bbox1.height));
}