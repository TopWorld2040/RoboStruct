﻿#include <numeric>
#include <unordered_set>

#include "wx/progdlg.h"
#include "wx/textdlg.h"

#include "EigenTools.hpp"
#include "MainFrame.hpp"

#include "Eigen/Core"
#include "Eigen/Dense"

#include "matrix.h"

#ifdef _DEBUG
	#define _CRTDBG_MAP_ALLOC
	#include <stdlib.h>
	#include <crtdbg.h>
	#ifndef DBG_NEW
		#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
		#define new DBG_NEW
	#endif
#endif  // _DEBUG

const wxEventTypeTag<wxThreadEvent> wxEVT_THREAD_UPDATE(wxNewEventType());

MainFrame::MainFrame(wxWindow* parent)
	: MainFrame_base(parent)
	, m_options()
	, m_beginx(0.0f)
	, m_beginy(0.0f)
	, m_counter(0.0f)
	, m_has_images(false)
	, m_features_detected(false)
	, m_matches_loaded(false)
	, m_sfm_done(false)
	, m_desc_length(0)
{
#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	// Set default path
	m_path = (m_dir_picker->GetPath()).mb_str();

	m_turntable_timer		= new wxTimer(this, ID_TIMER_TURNTABLE);
	m_reset_viewport_timer	= new wxTimer(this, ID_TIMER_RESET_VIEWPORT);

	wxStreamToTextRedirector redirect(m_tc_log, &std::cerr);
	this->InitializeLog();
	this->InitializeOpenGL();
	this->InitializeScene();
	this->InitializeCameraDatabase();
	this->ResetGLCanvas();
	this->ResetOptions();

	m_rotate_cursor	= wxCursor("rotate_cursor");
	m_pan_cursor	= wxCursor("pan_cursor");
	m_zoom_cursor	= wxCursor("zoom_cursor");

	// Setup the image list
	m_img_ctrl->InsertColumn(0, "Name",			wxLIST_FORMAT_LEFT,	60);
	m_img_ctrl->InsertColumn(1, "Resolution",	wxLIST_FORMAT_LEFT,	80);
	m_img_ctrl->InsertColumn(2, "Focal (px)",	wxLIST_FORMAT_LEFT,	65);
	m_img_ctrl->InsertColumn(3, "# features",	wxLIST_FORMAT_LEFT,	65);

	this->Bind(wxEVT_PG_CHANGED,	&MainFrame::OnOptionsChanged, this);
	this->Bind(wxEVT_TIMER,			&MainFrame::OnTimerUpdate,	this);
	this->Bind(wxEVT_THREAD_UPDATE,	&MainFrame::OnThreadUpdate,	this);
}

MainFrame::~MainFrame()
{
	delete m_gl_context;
	delete m_turntable_timer;
	delete m_reset_viewport_timer;

	this->Unbind(wxEVT_PG_CHANGED,		&MainFrame::OnOptionsChanged,	this);
	this->Unbind(wxEVT_TIMER,			&MainFrame::OnTimerUpdate,		this);
	this->Unbind(wxEVT_THREAD_UPDATE,	&MainFrame::OnThreadUpdate,		this);
}

void MainFrame::OnThreadUpdate(wxThreadEvent& event)
{
	wxCriticalSectionLocker lock(m_points_cs);

	gly::Meshdata data;

	data.m_indices.resize(m_points.size());
	std::iota(data.m_indices.begin(), data.m_indices.end(), 0);

	data.m_vertices.reserve(m_points.size());
	for (const auto &point : m_points)
	{
		gly::Vertex vert;
		vert.m_position = glm::vec3(point.m_pos[0], point.m_pos[1], point.m_pos[2]);
		vert.m_color = glm::vec4(point.m_color[0], point.m_color[1], point.m_color[2], 1.0f);
		data.m_vertices.push_back(vert);
	}

	m_scene->GetNode("Points")->GetMesh()->ChangeData(data);

	m_gl_canvas->Refresh(false);
}

void MainFrame::InitializeLog()
{
	// Redirect log messages
	wxLog::SetActiveTarget(new wxLogTextCtrl(m_tc_log));
	wxLogMessage("Log initialized");
}

void MainFrame::InitializeCameraDatabase()
{
	// Try to read the camera data file
	if(ReadCamDBFile("CamDB.txt"))	wxLogMessage("%i entries found in CamDB.txt", static_cast<int>(m_camDB.size()));
	else							wxLogMessage("Error: Camera database not found!");
}

bool MainFrame::AddImage(const std::string filename, const std::string filename_short)
{
	ImageData img(filename, filename_short);

	if(!img.GetExifInfo())
	{
		wxLogMessage("Can't get Exif info");
		return false;
	} else
	{
		if (this->FindCameraInDatabase(img))
		{
			m_images.push_back(img);
			return true;
		} else
		{
			wxString caption;
			caption << img.m_camera.m_camera_make << img.m_camera.m_camera_model << ": " << "Camera model not found!";
			wxTextEntryDialog dlg(this, "Add CCD width (in mm) to database...\n(decimal point must be a dot!)", caption);

			CamDBEntry cam;

			while(true)
			{
				if (dlg.ShowModal() == wxID_CANCEL) return false;
				if (dlg.GetValue().ToCDouble(&cam.second)) break;
			}

			cam.first = img.m_camera.m_camera_make + img.m_camera.m_camera_model;
			m_camDB.push_back(cam);
			
			this->FindCameraInDatabase(img);
			this->AddCamDBFileEntry();

			m_images.push_back(img);
			return true;
		}
	}
}

bool MainFrame::FindCameraInDatabase(ImageData &img)
{
	auto found = std::find_if(m_camDB.begin(), m_camDB.end(), [&](CamDBEntry& entry)
	{
		return (entry.first.find(img.m_camera.m_camera_model) != std::string::npos);
	});

	if (found != m_camDB.end())
	{
		img.ConvertFocalPx(found->second);
		return true;
	} else
	{
		img.ConvertFocalPx(8.333);		// TODO: dirty hack for jpg images without EXIF tags
		return true;
	}

	return false;
}

void MainFrame::DetectFeaturesAll()
{
	int num_images = this->GetNumImages();

	// Show progress dialog
	wxProgressDialog dialog("Progress", "Detecting features...", num_images, this,
							wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_ELAPSED_TIME |
							wxPD_ESTIMATED_TIME | wxPD_REMAINING_TIME);

	// Detect features
	for (int i = 0; i < num_images; i++)
	{
		this->DetectFeatures(i);

		wxString numfeat;
		numfeat << this->GetNumKeys(i);
		m_img_ctrl->SetItem(i, 3, numfeat);
		dialog.Update(i + 1);
		wxSafeYield();
	}

	// Update program state
	m_features_detected = true;
}

void MainFrame::DetectFeatures(int img_idx)
{
	double time = (double)cv::getTickCount();
	m_images[img_idx].DetectFeatures(m_options);
	time = (double)cv::getTickCount() - time;

	wxLogMessage("[DetectFeatures] %s: found %i features in %.2f ms.",
		m_images[img_idx].m_filename_short.c_str(), GetNumKeys(img_idx), time * 1000.0 / cv::getTickFrequency());
}

void MainFrame::MatchAll()
{
	int num_images = this->GetNumImages();
	int num_pairs = (num_images * (num_images - 1)) / 2;
	int progress_idx = 0;

	// Clean up
	for (auto &image : m_images)
	{
		image.m_visible_points.clear();
		image.m_visible_keys.clear();
		image.m_key_flags.clear();
	}

	m_tracks.clear();
	m_matches = MatchTable(num_images);
	m_matches.RemoveAll();
	m_transforms.clear();
	
	// Show progress dialog
	wxProgressDialog dialog("Progress", "Matching images...", num_pairs, this,
							wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_ELAPSED_TIME |
							wxPD_ESTIMATED_TIME | wxPD_REMAINING_TIME);

	for (int i = 1; i < num_images; i++)
	{
		wxLogMessage("[MatchAll] Matching %s...", m_images[i].m_filename_short.c_str());
		
		m_images[i].LoadDescriptors();
		m_desc_length = m_images[i].m_desc_size;

		// Create a search index
		cv::Mat idx_desc(m_images[i].m_keys.size(), m_desc_length, CV_32F, (void*)m_images[i].m_descriptors.data());
		cv::flann::Index flann_index(idx_desc, cv::flann::KDTreeIndexParams(m_options.matching_trees));

		for (int j = 0; j < i; j++)
		{
			m_images[j].LoadDescriptors();
	
			// Setup query data
			int num_descriptors = (int)m_images[j].m_keys.size();
			cv::Mat query_desc(num_descriptors, m_desc_length, CV_32F, (void*)m_images[j].m_descriptors.data());
			cv::Mat indices(num_descriptors, 2, CV_32S);
			cv::Mat dists(num_descriptors, 2, CV_32F);

			// Match!
			double time = (double)cv::getTickCount();
			flann_index.knnSearch(query_desc, indices, dists, 2, cv::flann::SearchParams(m_options.matching_checks));
			time = (double)cv::getTickCount() - time;

			// Store putative matches in ptpairs
			IntPairVector tmp_matches;
			int *indices_ptr = indices.ptr<int>(0);
			float *dists_ptr = dists.ptr<float>(0);

			for (int k = 0; k < indices.rows; ++k) {
				if (dists_ptr[2 * k] < (m_options.matching_distance_ratio * dists_ptr[2 * k + 1])) {
					tmp_matches.push_back(IntPair(indices_ptr[2 * k], k));
				}
			}

			int num_putative = static_cast<int>(tmp_matches.size());

			// Find and delete double matches
			int num_pruned = this->PruneDoubleMatches(tmp_matches);

			// Compute the fundamental matrix and remove outliers
			int num_inliers = this->ComputeEpipolarGeometry(i, j, tmp_matches);

			// Compute transforms
			TransformInfo tinfo;
			MatchIndex midx(i, j);
			tinfo.m_inlier_ratio = this->ComputeHomography(i, j, tmp_matches);

			// Store matches and transforms
			if (num_inliers > m_options.matching_min_matches)
			{
				TransformsEntry trans_entry(midx, tinfo);
				m_transforms.insert(trans_entry);

				this->SetMatch(i, j);
				auto &matches = m_matches.GetMatchList(GetMatchIndex(i, j));

				matches.clear();
				matches.reserve(num_inliers);

				for (const auto &match : tmp_matches) matches.push_back(KeypointMatch(match.first, match.second));

				// Be verbose
				wxLogMessage("[MatchAll]    ...with %s: %i inliers (%i putative, %i duplicates pruned), ratio = %.2f    (%.2f ms)",
					m_images[j].m_filename_short.c_str(), num_inliers, num_putative, num_pruned, tinfo.m_inlier_ratio, time * 1000.0 / cv::getTickFrequency());
			} else
			{
				// Be verbose
				wxLogMessage("[MatchAll]    ...with %s: no match", m_images[j].m_filename_short.c_str());
			}

			m_images[j].ClearDescriptors();

			progress_idx++;
			dialog.Update(progress_idx);
			wxSafeYield();
		}
		m_images[i].ClearDescriptors();
	}

	this->MakeMatchListsSymmetric();
	this->ComputeTracks();
	m_matches.RemoveAll();

	m_matches_loaded = true;
}

int	MainFrame::PruneDoubleMatches(IntPairVector &matches)
{
	int num_before = matches.size();

	// Mark an index as duplicate if it's registered more than once
	std::unordered_set<int> duplicates;
	for(const auto& match : matches)
	{
		if (std::count_if(matches.begin(), matches.end(), [&](const IntPair& item) { return item.first == match.first; }) > 1) duplicates.insert(match.first);
	}
	
	auto found_in_duplicates = [&](const IntPair& match) { return duplicates.find(match.first) != duplicates.end(); };
	matches.erase(std::remove_if(matches.begin(), matches.end(), found_in_duplicates), matches.end());

	return num_before - matches.size();
}

int MainFrame::ComputeEpipolarGeometry(int idx1, int idx2, IntPairVector &matches)
{
	auto num_putative = matches.size();
	std::vector<cv::Point2f> points1, points2;
	std::vector<uchar> status;
	points1.reserve(num_putative);
	points2.reserve(num_putative);
	status.reserve(num_putative);

	const auto& keys1 = m_images[idx1].m_keys;
	const auto& keys2 = m_images[idx2].m_keys;

	for (const auto &match : matches)
	{
		points1.push_back(cv::Point2f(keys1[match.first].m_x, keys1[match.first].m_y));
		points2.push_back(cv::Point2f(keys2[match.second].m_x, keys2[match.second].m_y));
	}

	// Find the fundamental matrix
	double threshold(0.001 * std::max(m_images[idx1].GetWidth(), m_images[idx1].GetHeight()));
	auto Fmat = cv::findFundamentalMat(cv::Mat(points1, true), cv::Mat(points2, true), status, cv::FM_RANSAC, threshold);

	// Remove outliers from ptpairs
	auto matches_old = matches;
	matches.clear();

	for (int m = 0; m < (int)status.size(); m++) if (status[m] == 1) matches.push_back(matches_old[m]);

	return (int)matches.size();
}

double MainFrame::ComputeHomography(int idx1, int idx2, const IntPairVector &matches)
{
	auto num_matches = matches.size();
	std::vector<cv::Point2f> points1, points2;
	std::vector<uchar> status;
	points1.reserve(num_matches);
	points2.reserve(num_matches);
	status.reserve(num_matches);

	auto& keys1 = m_images[idx1].m_keys;
	auto& keys2 = m_images[idx2].m_keys;

	for (const auto &match : matches)
	{
		points1.push_back(cv::Point2f(keys1[match.first].m_x, keys1[match.first].m_y));
		points2.push_back(cv::Point2f(keys2[match.second].m_x, keys2[match.second].m_y));
	}

	// Find the homography matrix
	double threshold(0.001 * std::max(m_images[idx1].GetWidth(), m_images[idx1].GetHeight()));
	cv::Mat Hmat = cv::findHomography(cv::Mat(points1, true), cv::Mat(points2, true), status, cv::RANSAC, threshold);

	// Compute and return inlier ratio
	return std::count(status.begin(), status.end(), 1) / static_cast<double>(num_matches);
}

void MainFrame::ComputeTracks()
{
	wxLogMessage("[ComputeTracks] Computing tracks...");
	double time = (double)cv::getTickCount();

	int num_images = this->GetNumImages();

	// Clear all marks for new images
	for (int i = 0; i < num_images; i++)
	{
		// If this image has no neighbors, don't worry about its keys
		if (m_matches.GetNumNeighbors(i) == 0) continue;

		int num_features = m_images[i].m_keys.size();
		m_images[i].m_key_flags.resize(num_features);
	}

	int pt_idx = 0;

	// Sort all match lists
	for (int i = 0; i < num_images; i++)
	{
		std::for_each(m_matches.Begin(i), m_matches.End(i), [&](AdjListElem val)
		{
			auto &list = val.m_match_list;
			std::sort(list.begin(), list.end(), [](KeypointMatch k1, KeypointMatch k2) { return k1.m_idx1 < k2.m_idx1; });
		});
	}

	bool *img_marked = new bool[num_images];
	memset(img_marked, 0, num_images * sizeof(bool));

	std::vector<int> touched(num_images);
	std::vector<TrackData> tracks;

	for (int i = 0; i < num_images; i++)
	{
		// If this image has no neighbors, skip it
		if (!m_matches.GetNumNeighbors(i)) continue;

		int num_features = m_images[i].m_keys.size();

		for (int j = 0; j < num_features; j++)
		{
			ImageKeyVector features;
			std::queue<ImageKey> features_queue;

			// Check if this feature was visited
			if (m_images[i].m_key_flags[j]) continue;	// already visited this feature

			// Reset flags
			int num_touched = touched.size();
			for (int k = 0; k < num_touched; k++) img_marked[touched[k]] = false;
			touched.clear();

			// Do a breadth first search given this feature
			m_images[i].m_key_flags[j] = true;

			features.push_back(ImageKey(i, j));
			features_queue.push(ImageKey(i, j));

			img_marked[i] = true;
			touched.push_back(i);

			int num_rounds = 0;
			while (!features_queue.empty())
			{
				num_rounds++;

				ImageKey feature = features_queue.front();
				features_queue.pop();

				int img1 = feature.first;
				int f1 = feature.second;
				KeypointMatch dummy;
				dummy.m_idx1 = f1;

				// Check all adjacent images
				auto &nbrs = m_matches.GetNeighbors(img1);
				for (auto iter = nbrs.begin(); iter != nbrs.end(); iter++)
				{
					unsigned int k = iter->m_index;
					if (img_marked[k]) continue;

					MatchIndex base = GetMatchIndex(img1, k);

					auto &list = m_matches.GetMatchList(base);

					// Do a binary search for the feature
					auto p = std::equal_range(list.begin(), list.end(), dummy, [](KeypointMatch k1, KeypointMatch k2) { return k1.m_idx1 < k2.m_idx1; });

					if (p.first == p.second) continue;	// not found

					int idx2 = (p.first)->m_idx2;

					// Check if we visited this point already
					if (m_images[k].m_key_flags[idx2]) continue;

					// Mark and push the point
					m_images[k].m_key_flags[idx2] = true;
					features.push_back(ImageKey(k, idx2));
					features_queue.push(ImageKey(k, idx2));

					img_marked[k] = true;
					touched.push_back(k);
				}
			} // While loop

			if (features.size() >= 2)
			{
				tracks.push_back(TrackData(features));
				pt_idx++;
			}
		} // For loop over features

		wxLogMessage("[ComputeTracks] Got %i tracks after checking image %i", (int)tracks.size(), i);
	} // For loop over images

	if (pt_idx != (int)tracks.size()) wxLogMessage("[ComputeTracks] Error: point count inconsistent!");

	// Clear match lists
	wxLogMessage("[ComputeTracks] Clearing match lists...");
	m_matches.RemoveAll();

	// Create the new consistent match lists
	wxLogMessage("[ComputeTracks] Creating consistent match lists...");

	int num_pts = pt_idx;

	for (int i = 0; i < num_pts; i++)
	{
		for (std::size_t j = 0; j < tracks[i].m_views.size(); j++)
		{
			int img1 = tracks[i].m_views[j].first;
			int key1 = tracks[i].m_views[j].second;

			m_images[img1].m_visible_points.push_back(i);
			m_images[img1].m_visible_keys.push_back(key1);
		}
	}

	// Save the tracks
	m_tracks = tracks;

	time = (double)cv::getTickCount() - time;

	wxLogMessage("[ComputeTracks] Found %i tracks in %.2f ms", (int)m_tracks.size(), time * 1000.0 / cv::getTickFrequency());

	// Print track stats
	std::vector<int> stats(num_images + 1);
	for (auto track : m_tracks) stats[track.m_views.size()] += 1;
	for (int i = 2; i < (num_images + 1); i++) wxLogMessage("[ComputeTracks] %i projections: %i", i, stats[i]);
}

void MainFrame::MakeMatchListsSymmetric()
{
	unsigned int num_images = GetNumImages();

	std::vector<MatchIndex> matches;

	for (unsigned int i = 0; i < num_images; i++)
	{
		for (auto iter = m_matches.Begin(i); iter != m_matches.End(i); ++iter)
		{
			unsigned int j = iter->m_index;

			MatchIndex idx = GetMatchIndex(i, j);
			MatchIndex idx_rev = GetMatchIndex(j, i);

			auto &list = iter->m_match_list;
			unsigned int num_matches = list.size();

			m_matches.SetMatch(idx_rev);
			m_matches.ClearMatch(idx_rev);

			for (unsigned int k = 0; k < num_matches; k++)
			{
				KeypointMatch m1, m2;

				m1 = list[k];

				m2.m_idx1 = m1.m_idx2;
				m2.m_idx2 = m1.m_idx1;

				m_matches.AddMatch(idx_rev, m2);
			}

			matches.push_back(idx);
		}
	}

	for (auto match : matches) this->SetMatch(static_cast<int>(match.second), static_cast<int>(match.first));

	matches.clear();
}

int MainFrame::GetNumTrackMatches(int img1, int img2)
{
	int num_intersections = 0;

	const auto &tracks1 = m_images[img1].m_visible_points;
	const auto &tracks2 = m_images[img2].m_visible_points;

	for (auto track_idx : tracks2) m_tracks[track_idx].m_extra = 0;
	for (auto track_idx : tracks1) m_tracks[track_idx].m_extra = 1;
	for (auto track_idx : tracks2) num_intersections += m_tracks[track_idx].m_extra;
	for (auto &track : m_tracks) track.m_extra = -1;

	return num_intersections;
}

double MainFrame::GetInlierRatio(int idx1, int idx2)
{
	double inlier_ratio = 0.0;

	auto match = m_transforms.find(MatchIndex(idx1, idx2));

	if (match != m_transforms.end())
	{
		inlier_ratio = match->second.m_inlier_ratio;
	} else
	{
		match = m_transforms.find(MatchIndex(idx2, idx1));
		if (match != m_transforms.end())
		{
			inlier_ratio = match->second.m_inlier_ratio;
		}
	}

	return inlier_ratio;
}