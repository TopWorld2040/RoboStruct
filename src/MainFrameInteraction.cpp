#include <numeric>

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/compatibility.hpp"
#include "glm/gtx/epsilon.hpp"
#include "glm/gtx/quaternion.hpp"

#include "wx/dir.h"

#include "MainFrame.hpp"

void MainFrame::OnUpdateUI(wxUpdateUIEvent& event)
{
	int id = event.GetId();

	switch(id)
	{
	case ID_RECONSTRUCT:					event.Enable(m_has_images);			break;

	case ID_EXPORT_MATCHES:					event.Enable(m_matches_loaded);		break;
	case ID_EXPORT_TRACKS:					event.Enable(m_matches_loaded);		break;
	case ID_EXPORT_CMVS:					event.Enable(m_sfm_done);			break;
	case ID_EXPORT_BUNDLE_FILE:				event.Enable(m_sfm_done);			break;
	case ID_EXPORT_PLY_FILE:				event.Enable(m_sfm_done);			break;
	case ID_EXPORT_MAYA_FILE:				event.Enable(m_sfm_done);			break;

	case ID_TOGGLE_FRUSTRUM_VISIBILITY:		event.Enable(m_sfm_done);			break;
	case ID_TOGGLE_POINTS_VISIBILITY:		event.Enable(m_sfm_done);			break;
	case ID_TOGGLE_CAMERAS_VISIBILITY:		event.Enable(m_sfm_done);			break;

	case ID_PANE_MATCHES:					event.Enable(m_sfm_done);			break;
	default:								event.Skip();
	}
}

void MainFrame::OnMenuExit(wxCommandEvent& event)
{
	this->Close();
}

void MainFrame::OnClose(wxCloseEvent& event)
{
	m_turntable_timer->Stop();
	m_reset_viewport_timer->Stop();

	this->Destroy();
}

void MainFrame::OnViewWindows(wxCommandEvent& event)
{
	int id = event.GetId();

	switch(id)
	{
	case ID_VIEW_IMAGE_BROWSER:
		m_mgr.GetPane("Image browser").Show();
		m_mgr.Update();
		break;
	case ID_VIEW_IMAGE_PREVIEW:
		m_mgr.GetPane("Image preview").Show();
		m_mgr.Update();
		break;
	case ID_VIEW_OPTIONS:
		m_mgr.GetPane("Options").Show();
		m_mgr.Update();
		break;
	case ID_VIEW_LOG:
		m_mgr.GetPane("Log").Show();
		m_mgr.Update();
		break;
	case ID_VIEW_ABOUT:
		m_mgr.GetPane("About").Show();
		m_mgr.Update();
		break;
	default: event.Skip();
	}
}

void MainFrame::OnResetOptions(wxCommandEvent& event)
{
	this->ResetOptions();
}

void MainFrame::OnReset3dViewport(wxCommandEvent& event)
{
	if (m_reset_viewport_timer->IsRunning())	m_reset_viewport_timer->Stop();
	else
	{
		m_counter = 0.0f;
		m_reset_viewport_timer->Start(10);
	}
}

void MainFrame::OnChangeMSAASamples(wxSpinEvent& event)
{
	// TODO
	//m_mgr.GetPane("Viewport").DestroyOnClose(true);
	//m_mgr.ClosePane(m_mgr.GetPane("Viewport"));

	//m_gl_canvas->Close();
	//delete m_gl_context;

	//int GLCanvasAttributes[] = {WX_GL_RGBA, 1, WX_GL_DOUBLEBUFFER, 1, WX_GL_SAMPLES, event.GetValue(), 0};
	//m_gl_canvas = new wxGLCanvas(this, -1, GLCanvasAttributes);
	//m_gl_context = new wxGLContext(m_gl_canvas);
	//m_gl_canvas->SetCurrent(*m_gl_context);
	//m_mgr.AddPane(m_gl_canvas, wxAuiPaneInfo().CentrePane().Caption("Viewport").CaptionVisible(true).MaximizeButton(true).MinimizeButton(false).PinButton(false).CloseButton(false));
	//m_mgr.Update();
}

void MainFrame::OnToggleTurntableAnimation(wxCommandEvent& event)
{
	if (!m_turntable_timer->IsRunning())	m_turntable_timer->Start(10);
	else									m_turntable_timer->Stop();

	m_gl_canvas->Refresh(false);
}

void MainFrame::OnToggleVisibility(wxCommandEvent& event)
{
	switch(event.GetId())
	{
	case ID_TOGGLE_TRACKBALL_VISIBILITY:
		if (m_scene->GetNode("Trackball X")->IsVisibleMesh() &&
			m_scene->GetNode("Trackball Y")->IsVisibleMesh() &&
			m_scene->GetNode("Trackball Z")->IsVisibleMesh())
		{
			m_scene->GetNode("Trackball X")->SetVisibilityMesh(false);
			m_scene->GetNode("Trackball Y")->SetVisibilityMesh(false);
			m_scene->GetNode("Trackball Z")->SetVisibilityMesh(false);
		} else
		{
			m_scene->GetNode("Trackball X")->SetVisibilityMesh(true);
			m_scene->GetNode("Trackball Y")->SetVisibilityMesh(true);
			m_scene->GetNode("Trackball Z")->SetVisibilityMesh(true);
		}
		break;
	case ID_TOGGLE_GRID_VISIBILITY:
		{
			auto grid = m_scene->GetNode("Grid");
			if (grid->IsVisibleMesh())	grid->SetVisibilityMesh(false);
			else						grid->SetVisibilityMesh(true);
			break;
		}
	//case ID_TOGGLE_NODE_VISIBILITY:
	//	{
	//		auto sel = m_scene_browser->GetSelection();
	//		auto name = m_scene_browser->GetItemText(sel).ToStdString();
	//		auto node = m_scene->GetNode(name);

	//		if (node->IsVisibleMesh())	node->SetVisibilityMesh(false);
	//		else						node->SetVisibilityMesh(true);
	//		
	//		break;
	//	}
	default: return;
	}

	m_gl_canvas->Refresh(false);
}

void MainFrame::OnTimerUpdate(wxTimerEvent& event)
{
	switch (event.GetId())
	{
	case ID_TIMER_TURNTABLE:
		{
			auto camera		= m_scene->GetCamera();
			auto rotation	= camera->GetTrackballOrientation();

			camera->RotateTrackball(glm::vec2(0.0f, 0.0f), glm::vec2(-0.0002f * m_tb_turntable_speed_slider->GetValue(), 0.0f));

			m_scene->GetNode("Trackball X")->GetTransform().SetOrientation(rotation * glm::angleAxis(90.0f, glm::vec3(0.0f, 1.0f, 0.0f)));
			m_scene->GetNode("Trackball Y")->GetTransform().SetOrientation(rotation * glm::angleAxis(90.0f, glm::vec3(1.0f, 0.0f, 0.0f)));
			m_scene->GetNode("Trackball Z")->GetTransform().SetOrientation(rotation);

			break;
		}
	case ID_TIMER_RESET_VIEWPORT:
		{
			m_counter += 0.02f;
			if (m_counter >= 1.0f) m_reset_viewport_timer->Stop();

			auto camera = m_scene->GetCamera();
			auto quat_x = glm::angleAxis(15.0f, glm::vec3(1, 0, 0));
			auto quat_y = glm::angleAxis(45.0f, glm::vec3(0, 1, 0));
			auto quat_z = glm::angleAxis(0.0f, glm::vec3(0, 0, 1));

			auto orientation_start	= camera->GetTrackballOrientation();
			auto orientation_target	= quat_x * quat_y * quat_z;

			auto position_start		= camera->GetTrackballPosition();
			auto position_target	= glm::vec3(0.0f, 0.0f, 0.0f);

			auto zoom_start			= camera->GetTrackballZoom();
			auto zoom_target		= 1.0f;

			auto zoom_mix			= glm::lerp(zoom_start, zoom_target, m_counter);
			auto position_mix		= glm::lerp(position_start, position_target, m_counter);
			auto orientation_mix	= glm::shortMix(orientation_start, orientation_target, m_counter);

			camera->SetTrackballZoom(zoom_mix);
			camera->SetTrackballPosition(position_mix);
			camera->SetTrackballOrientation(orientation_mix);

			auto rotation = camera->GetTrackballOrientation();
			m_scene->GetNode("Trackball X")->GetTransform().SetOrientation(orientation_mix * glm::angleAxis(90.0f, glm::vec3(0.0f, 1.0f, 0.0f)));
			m_scene->GetNode("Trackball Y")->GetTransform().SetOrientation(orientation_mix * glm::angleAxis(90.0f, glm::vec3(1.0f, 0.0f, 0.0f)));
			m_scene->GetNode("Trackball Z")->GetTransform().SetOrientation(orientation_mix);

			m_beginx = m_beginy = 0.0f;

			break;
		}
	default: event.Skip();
	}

	m_gl_canvas->Refresh(false);
}

void MainFrame::OnExport(wxCommandEvent& event)
{
	int id = event.GetId();

	switch(id)
	{
	case ID_EXPORT_TRACKS:
		{
			this->SaveTrackFile();
			break;
		}
	case ID_EXPORT_MATCHES:
		{
			this->SaveMatchFile();
			break;
		}
	case ID_EXPORT_CMVS:
		{
			this->ExportToCMVS();
			break;
		}
	case ID_EXPORT_BUNDLE_FILE:
		{
			this->SaveBundleFile(m_path);
			break;
		}
	case ID_EXPORT_PLY_FILE:
		{
			this->SavePlyFile();
			break;
		}
	case ID_EXPORT_MAYA_FILE:
		{
			this->SaveMayaFile();
			break;
		}
	default: event.Skip();
	}
}

void MainFrame::OnSaveLog(wxCommandEvent& event)
{
	auto target = m_path + "\\Log.txt";
	if (m_tc_log->SaveFile(target)) wxLogMessage("Log saved to %s", target);
}

void MainFrame::OnClearLog(wxCommandEvent& event)
{
	m_tc_log->Clear();
}

void MainFrame::OnSelectDirectory(wxFileDirPickerEvent& event)
{
	wxDir dir(m_dir_picker->GetPath());
	wxString filename, focalPx, res;

	m_images.clear();

	m_cb_matches_left->Clear();
	m_cb_matches_right->Clear();
	m_window_image_preview->Refresh(true);
	m_pane_matches_view->Refresh(true);

	m_path = (m_dir_picker->GetPath()).ToStdString();

	m_img_ctrl->ClearAll();
	m_img_ctrl->InsertColumn(0, "Name",			wxLIST_FORMAT_LEFT, 60);
	m_img_ctrl->InsertColumn(1, "Resolution",	wxLIST_FORMAT_LEFT, 80);
	m_img_ctrl->InsertColumn(2, "Focal (px)",	wxLIST_FORMAT_LEFT, 65);
	m_img_ctrl->InsertColumn(3, "# features",	wxLIST_FORMAT_LEFT, 65);

	// Parse directory and process jpg images
	bool found = dir.GetFirst(&filename, "*.jpg", wxDIR_FILES);
	int index = 0;

	while (found)
	{
		// Get focal from EXIF tags, convert to px-coordinates and add image if successful
		if(this->AddImage(dir.FindFirst(m_dir_picker->GetPath(), filename).ToStdString(), filename.ToStdString()))
		{
			m_img_ctrl->InsertItem(index, filename);
			m_cb_matches_left->Append(filename);
			m_cb_matches_right->Append(filename);
		}

		// Get the next jpg in the directory
		found = dir.GetNext(&filename);
		index++;
	}

	// Display findings
	if(m_images.size() > 0)
	{
		for (int i = 0; i < (int)m_images.size(); i++)
		{
			res.Printf("%i x %i", this->GetImageWidth(i), this->GetImageHeight(i));
			focalPx.Printf("%.2f", this->GetFocalLength(i));
			m_img_ctrl->SetItem(i, 1, res, -1);
			m_img_ctrl->SetItem(i, 2, focalPx, -1);
		}

		m_has_images = true;
		wxLogMessage("%s: %i images ready for reconstruction", (m_dir_picker->GetPath()).mb_str(), m_images.size());
	} else
	{
		wxLogMessage("No suitable images found in %s", (m_dir_picker->GetPath()).mb_str());
	}
}

void MainFrame::OnReconstruct(wxCommandEvent& event)
{
	// Detect features
	wxLogMessage("Detecting features...");
	double time = (double)cv::getTickCount();
	this->DetectFeaturesAll();
	time = (double)cv::getTickCount() - time;
	wxLogMessage("Feature detection took %.2f s", time / cv::getTickFrequency());

	// Match features
	wxLogMessage("Matching images...");
	time = (double)cv::getTickCount();
	if (m_options.feature_type != 3)	this->MatchAll();
	time = (double)cv::getTickCount() - time;
	wxLogMessage("Matching all images took %.2f s", time / cv::getTickFrequency());

	// Compute structure from motion
	this->StartBundlerThread();
}