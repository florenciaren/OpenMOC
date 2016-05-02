#include "TraverseSegments.h"

/**
 * @brief Constructor for the TravseSegments class assigns the TrackGenerator
 *        and pulls relevant information from it.
 */
TraverseSegments::TraverseSegments(TrackGenerator* track_generator) {

  /* Save the track generator */
  _track_generator = track_generator;

  /* Determine the type of segment formation used */
  _segment_formation = track_generator->getSegmentFormation();

  /* Determine if a global z-mesh is used for 3D calculations */
  _track_generator_3D = dynamic_cast<TrackGenerator3D*>(track_generator);
  if (_track_generator_3D != NULL)
    _track_generator_3D->retrieveGlobalZMesh(_global_z_mesh, _mesh_size);
}


/**
 * @brief Destructor for TraverseSegments
 */
TraverseSegments::~TraverseSegments() {
}


/**
 * @breif Loops over Tracks, applying the provided kernels to all segments and
 *        the functionality described in onTrack(...) to all Tracks.
 * @details The segment formation method imported from the TrackGenerator
 *          during construction is used to redirect to the appropriate looping
 *          scheme. If kernels are provided (not NULL) then they are deleted at
 *          the end of the looping scheme.
 * @param kernels MOCKernels to apply to all segments
 */
void TraverseSegments::loopOverTracks(MOCKernel** kernels) {

  switch (_segment_formation) {
    case EXPLICIT_2D:
      loopOverTracks2D(kernels);
      break;
    case EXPLICIT_3D:
      loopOverTracksExplicit(kernels);
      break;
    case OTF_TRACKS:
      loopOverTracksByTrackOTF(kernels);
      break;
    case OTF_STACKS:
      loopOverTracksByStackOTF(kernels);
      break;
  }

  if (kernels != NULL) {
    int num_rows = 1;
    if (_track_generator_3D != NULL)
      num_rows = _track_generator_3D->getNumRows();
    for (int z=0; z < num_rows; z++)
      delete kernels[z];
    delete [] kernels;
  }
}


/**
 * @brief Loops over all explicit 2D Tracks
 * @details The onTrack(...) function is applied to all 2D Tracks and the
 *          specified kernels are applied to all segments. If NULL is provided
 *          for the kernels, only the onTrack(...) functionality is applied.
 * @param kernels The MOCKernels dictating the functionality to apply to
 *        segments
 */
void TraverseSegments::loopOverTracks2D(MOCKernel** kernels) {

  /* Loop over all parallel tracks for each azimuthal angle */
  Track** tracks_2D = _track_generator->get2DTracks();
  int num_azim = _track_generator->getNumAzim();
  for (int a=0; a < num_azim/2; a++) {
    int num_xy = _track_generator->getNumX(a) + _track_generator->getNumY(a);
#pragma omp for
    for (int i=0; i < num_xy; i++) {

      Track* track_2D = &tracks_2D[a][i];
      segment* segments = track_2D->getSegments();

      /* Operate on segments if necessary */
      if (kernels != NULL) {
        kernels[0]->newTrack(track_2D);
        traceSegmentsExplicit(track_2D, kernels[0]);
      }

      /* Operate on the Track */
      onTrack(track_2D, segments);
    }
  }
}


/**
 * @brief Loops over all explicit 3D Tracks
 * @details The onTrack(...) function is applied to all 3D Tracks and the
 *          specified kernels are applied to all segments. If NULL is provided
 *          for the kernels, only the onTrack(...) functionality is applied.
 * @param kernels The MOCKernels dictating the functionality to apply to
 *        segments
 */
void TraverseSegments::loopOverTracksExplicit(MOCKernel** kernels) {

  Track3D**** tracks_3D = _track_generator_3D->get3DTracks();
  int num_azim = _track_generator_3D->getNumAzim();
  int num_polar = _track_generator_3D->getNumPolar();
  int*** tracks_per_stack = _track_generator_3D->getTracksPerStack();

  /* Loop over all tracks, parallelizing over parallel 2D tracks */
  for (int a=0; a < num_azim/2; a++) {
    int num_xy = _track_generator->getNumX(a) + _track_generator->getNumY(a);
#pragma omp for
    for (int i=0; i < num_xy; i++) {

      /* Loop over polar angles */
      for (int p=0; p < num_polar; p++) {

        /* Loop over tracks in the z-stack */
        for (int z=0; z < tracks_per_stack[a][i][p]; z++) {

          /* Extract 3D track and initialize segments pointer */
          Track* track_3D = &tracks_3D[a][i][p][z];

          /* Operate on segments if necessary */
          if (kernels != NULL) {

            /* Reset kernel for a new Track */
            kernels[0]->newTrack(track_3D);

            /* Trace the segments on the track */
            traceSegmentsExplicit(track_3D, kernels[0]);
          }

          /* Operate on the Track */
          segment* segments = track_3D->getSegments();
          onTrack(track_3D, segments);
        }
      }
    }
  }
}


/**
 * @brief Loops over all 3D Tracks using axial on-the-fly ray tracking by Track
 * @details The onTrack(...) function is applied to all 3D Tracks and the
 *          specified kernels are applied to all segments. If NULL is provided
 *          for the kernels, only the onTrack(...) functionality is applied.
 * @param kernels The MOCKernels dictating the functionality to apply to
 *        segments
 */
void TraverseSegments::loopOverTracksByTrackOTF(MOCKernel** kernels) {

  int num_2D_tracks = _track_generator_3D->getNum2DTracks();
  Track** flattened_tracks = _track_generator_3D->get2DTracksArray();
  Track3D**** tracks_3D = _track_generator_3D->get3DTracks();
  int*** tracks_per_stack = _track_generator_3D->getTracksPerStack();
  int num_polar = _track_generator_3D->getNumPolar();
  int tid = omp_get_thread_num();

#pragma omp for
  /* Loop over flattened 2D tracks */
  for (int ext_id=0; ext_id < num_2D_tracks; ext_id++) {

    /* Extract indices of 3D tracks associated with the flattened track */
    Track* flattened_track = flattened_tracks[ext_id];
    int a = flattened_track->getAzimIndex();
    int i = flattened_track->getXYIndex();

    /* Loop over polar angles */
    for (int p=0; p < num_polar; p++) {

      /* Loop over tracks in the z-stack */
      for (int z=0; z < tracks_per_stack[a][i][p]; z++) {

        /* Extract 3D track and initialize segments pointer */
        Track* track_3D = &tracks_3D[a][i][p][z];

        /* Operate on segments if necessary */
        if (kernels != NULL) {

          /* Reset kernel for a new Track */
          kernels[0]->newTrack(track_3D);
          double theta = tracks_3D[a][i][p][z].getTheta();
          Point* start = track_3D->getStart();

          /* Trace the segments on the track */
          traceSegmentsOTF(flattened_track, start, theta, kernels[0]);
          track_3D->setNumSegments(kernels[0]->getCount());
        }

        /* Operate on the Track */
        segment* segments = _track_generator_3D->getTemporarySegments(tid, 0);
        onTrack(track_3D, segments);
      }
    }
  }
}


/**
 * @brief Loops over all 3D Tracks using axial on-the-fly ray tracking by
 *        z-stack
 * @details The onTrack(...) function is applied to all 3D Tracks and the
 *          specified kernels are applied to all segments. If NULL is provided
 *          for the kernels, only the onTrack(...) functionality is applied.
 * @param kernels The MOCKernels dictating the functionality to apply to
 *        segments
 */
void TraverseSegments::loopOverTracksByStackOTF(MOCKernel** kernels) {

  int num_2D_tracks = _track_generator_3D->getNum2DTracks();
  Track** flattened_tracks = _track_generator_3D->get2DTracksArray();
  Track3D**** tracks_3D = _track_generator_3D->get3DTracks();
  int*** tracks_per_stack = _track_generator_3D->getTracksPerStack();
  int num_polar = _track_generator_3D->getNumPolar();
  int tid = omp_get_thread_num();

#pragma omp for
  /* Loop over flattened 2D tracks */
  for (int ext_id=0; ext_id < num_2D_tracks; ext_id++) {

    /* Extract indices of 3D tracks associated with the flattened track */
    Track* flattened_track = flattened_tracks[ext_id];
    int a = flattened_track->getAzimIndex();
    int i = flattened_track->getXYIndex();

    /* Loop over polar angles */
    for (int p=0; p < num_polar; p++) {

      /* Trace all tracks in the z-stack if necessary */
      Track* track_3D = &tracks_3D[a][i][p][0];
      if (kernels != NULL) {

        /* Reset kernels to their new Track */
        kernels[0]->newTrack(track_3D);

        /* Trace all segments in the z-stack */
        traceStackOTF(flattened_track, p, kernels);
        track_3D->setNumSegments(kernels[0]->getCount());
      }

      /* Operate on the Track */
      segment* segments = _track_generator_3D->getTemporarySegments(tid, 0);
      onTrack(track_3D, segments);
    }
  }
}


/**
 * @brief Loops over segments in a Track when segments are explicitly generated
 * @details All segments in the provided Track are looped over and the provided
 *          MOCKernel is applied to them.
 * @param track The Track whose segments will be traversed
 * @param kernel The kernel to apply to all segments
 */
void TraverseSegments::traceSegmentsExplicit(Track* track, MOCKernel* kernel) {
  for (int s=0; s < track->getNumSegments(); s++) {
    segment* seg = track->getSegment(s);
    kernel->execute(seg->_length, seg->_material, seg->_region_id, 0,
                    seg->_cmfd_surface_fwd, seg->_cmfd_surface_bwd);
  }
}


/**
 * @brief Computes 3D segment lengths on-the-fly for a single 3D track given an
 *        associated 2D Track with a starting point and a polar angle. The
 *        computed segments are passed to the provided kernel.
 * @details Segment lengths are computed on-the-fly using 2D segment lengths
 *          stored in a 2D Track object and 1D meshes from the extruded
 *          FSRs. Note: before calling this funciton with a SegmentationKernel,
 *          the memory for the segments should be allocated and referenced by
 *          the kernel using the setSegments routine in the kernels.
 * @param flattened_track the 2D track associated with the 3D track for which
 *        3D segments are computed
 * @param start the starting coordinates of the 3D track
 * @param theta the polar angle of the 3D track
 * @param kernel An MOCKernel object to apply to the calculated 3D segments
 */
void TraverseSegments::traceSegmentsOTF(Track* flattened_track, Point* start,
                                        double theta, MOCKernel* kernel) {

  /* Create unit vector */
  double phi = flattened_track->getPhi();
  double cos_theta = cos(theta);
  double sin_theta = sin(theta);
  int sign = (cos_theta > 0) - (cos_theta < 0);

  /* Extract starting coordinates */
  double x_start_3D = start->getX();
  double x_start_2D = flattened_track->getStart()->getX();
  double z_coord = start->getZ();

  /* Find 2D distance from 2D edge to start of track */
  double start_dist_2D = (x_start_3D - x_start_2D) / cos(phi);

  /* Find starting 2D segment */
  int seg_start = 0;
  segment* segments_2D = flattened_track->getSegments();
  for (int s=0; s < flattened_track->getNumSegments(); s++) {

    /* Determine if start point of track is beyond current 2D segment */
    double seg_len_2D = segments_2D[s]._length;
    if (start_dist_2D > seg_len_2D) {
      start_dist_2D -= seg_len_2D;
      seg_start++;
    }
    else {
      break;
    }
  }

  Geometry* geometry = _track_generator_3D->getGeometry();
  Cmfd* cmfd = geometry->getCmfd();

  /* Extract the appropriate starting mesh */
  int num_fsrs;
  FP_PRECISION* axial_mesh;
  bool contains_global_z_mesh;
  if (_global_z_mesh != NULL) {
    contains_global_z_mesh = true;
    num_fsrs = _mesh_size;
    axial_mesh = _global_z_mesh;
  }
  else {
    contains_global_z_mesh = false;
    int extruded_fsr_id = segments_2D[seg_start]._region_id;
    ExtrudedFSR* extruded_FSR = geometry->getExtrudedFSR(extruded_fsr_id);
    num_fsrs = extruded_FSR->_num_fsrs;
    axial_mesh = extruded_FSR->_mesh;
  }

  /* Get the starting z index */
  int z_ind = findMeshIndex(axial_mesh, num_fsrs+1, z_coord, sign);

  /* Loop over 2D segments */
  bool first_segment = true;
  bool segments_complete = false;
  for (int s=seg_start; s < flattened_track->getNumSegments(); s++) {

    /* Extract extruded FSR */
    int extruded_fsr_id = segments_2D[s]._region_id;
    ExtrudedFSR* extruded_FSR = geometry->getExtrudedFSR(extruded_fsr_id);

    /* Determine new mesh and z index */
    if (first_segment || contains_global_z_mesh) {
      first_segment = false;
    }
    else {
      /* Determine the axial region */
      num_fsrs = extruded_FSR->_num_fsrs;
      axial_mesh = extruded_FSR->_mesh;
      z_ind = findMeshIndex(axial_mesh, num_fsrs+1, z_coord, sign);
    }

    /* Extract 2D segment length */
    double remaining_length_2D = segments_2D[s]._length - start_dist_2D;
    start_dist_2D = 0;

    /* Transport along the 2D segment until it is completed */
    while (remaining_length_2D > 0) {

      /* Calculate 3D distance to z intersection */
      double z_dist_3D;
      if (sign > 0)
        z_dist_3D = (axial_mesh[z_ind+1] - z_coord) / cos_theta;
      else
        z_dist_3D = (axial_mesh[z_ind] - z_coord) / cos_theta;

      /* Calculate 3D distance to end of segment */
      double seg_dist_3D = remaining_length_2D / sin_theta;

      /* Calcualte shortest distance to intersection */
      double dist_2D;
      double dist_3D;
      int z_move;
      if (z_dist_3D <= seg_dist_3D) {
        dist_2D = z_dist_3D * sin_theta;
        dist_3D = z_dist_3D;
        z_move = sign;
      }
      else {
        dist_2D = remaining_length_2D;
        dist_3D = seg_dist_3D;
        z_move = 0;
      }

      /* Get the 3D FSR */
      int fsr_id = extruded_FSR->_fsr_ids[z_ind];

      /* Calculate CMFD surface */
      int cmfd_surface_bwd = -1;
      int cmfd_surface_fwd = -1;
      if (cmfd != NULL && dist_3D > TINY_MOVE) {

        /* Determine if this is the first 3D segment handled for the flattened
           2D segment. If so, get the 2D cmfd surface. */
        if (segments_2D[s]._length - remaining_length_2D <= TINY_MOVE)
          cmfd_surface_bwd = segments_2D[s]._cmfd_surface_bwd;

        /* Determine if this is the last 3D segment handled for the flattened
           2D segment. If so, get the 2D cmfd surface. */
        double next_dist_3D = (remaining_length_2D - dist_2D) / sin_theta;
        if (z_move == 0 || next_dist_3D <= TINY_MOVE)
          cmfd_surface_fwd = segments_2D[s]._cmfd_surface_fwd;

        /* Get CMFD cell */
        int cmfd_cell = geometry->getCmfdCell(fsr_id);

        /* Find the backwards surface */
        cmfd_surface_bwd = cmfd->findCmfdSurfaceOTF(cmfd_cell, z_coord,
                                                    cmfd_surface_bwd);

        /* Move axial height to end of segment */
        z_coord += cos_theta * dist_3D;

        /* Find forward surface */
        cmfd_surface_fwd = cmfd->findCmfdSurfaceOTF(cmfd_cell, z_coord,
                                                    cmfd_surface_fwd);
      }
      else {
        /* Move axial height to end of segment */
        z_coord += dist_3D * cos_theta;
      }

      /* Operate on segment */
      if (dist_3D > TINY_MOVE)
        kernel->execute(dist_3D, extruded_FSR->_materials[z_ind], fsr_id, 0,
                        cmfd_surface_fwd, cmfd_surface_bwd);

      /* Shorten remaining 2D segment length and move axial level */
      remaining_length_2D -= dist_2D;
      z_ind += z_move;

      /* Check if the track has crossed a Z boundary */
      if (z_ind < 0 or z_ind >= num_fsrs) {

        /* Reset z index */
        if (z_ind < 0)
          z_ind = 0;
        else
          z_ind = num_fsrs - 1;

        /* Mark the 2D segment as complete */
        segments_complete = true;
        break;
      }
    }

    /* Check if the track is completed due to an axial boundary */
    if (segments_complete)
      break;
  }
}


/**
 * @brief Computes 3D segment lengths on-the-fly for all tracks in a z-stack
 *        for a given associated 2D Track and a polar index on-the-fly and
 *        passes the computed segments to the provided kernels.
 * @details Segment lengths are computed on-the-fly using 2D segment lengths
 *          stored in a 2D Track object and 1D meshes from the extruded
 *          FSRs. Note: before calling this funciton with SegmentationKernels,
 *          the memory for the segments should be allocated and referenced by
 *          the kernels using the setSegments routine in the kernels.
 * @param flattened_track the 2D track associated with the z-stack for which
 *        3D segments are computed
 * @param polar_index the index into the polar angles which is associated with
 *        the polar angle of the z-stack
 * @param kernels An array of MOCKernel objects to apply to the calculated 3D
 *        segments
 */
void TraverseSegments::traceStackOTF(Track* flattened_track, int polar_index,
                                     MOCKernel** kernels) {

  /* Extract information about the z-stack */
  int azim_index = flattened_track->getAzimIndex();
  int track_index = flattened_track->getXYIndex();
  int*** tracks_per_stack = _track_generator_3D->getTracksPerStack();
  int num_z_stack = tracks_per_stack[azim_index][track_index][polar_index];
  Track3D**** tracks_3D = _track_generator_3D->get3DTracks();
  Track3D* first = &tracks_3D[azim_index][track_index][polar_index][0];
  double theta = first->getTheta();
  double z_spacing = _track_generator_3D->getZSpacing(azim_index, polar_index);

  /* Create unit vector */
  double phi = flattened_track->getPhi();
  double cos_theta = cos(theta);
  double sin_theta = sin(theta);
  double tan_theta = sin_theta / cos_theta;
  int sign = (cos_theta > 0) - (cos_theta < 0);
  double track_spacing_3D = z_spacing / std::abs(cos_theta);

  /* Find 2D distance from 2D edge to start of track */
  double x_start_3D = first->getStart()->getX();
  double x_start_2D = flattened_track->getStart()->getX();
  double start_dist_2D = (x_start_3D - x_start_2D) / cos(phi);

  /* Calculate starting intersection of lowest track with z-axis */
  double z0 = first->getStart()->getZ();
  double start_z = z0 - start_dist_2D / tan_theta;

  /* Get the Geometry and CMFD mesh */
  Geometry* geometry = _track_generator_3D->getGeometry();
  Cmfd* cmfd = geometry->getCmfd();

  /* Extract the appropriate starting mesh */
  int num_fsrs;
  FP_PRECISION* axial_mesh;
  if (_global_z_mesh != NULL) {
    num_fsrs = _mesh_size;
    axial_mesh = _global_z_mesh;
  }

  /* Loop over 2D segments */
  double first_start_z = start_z;
  segment* segments_2D = flattened_track->getSegments();
  for (int s=0; s < flattened_track->getNumSegments(); s++) {

    /* Get segment length and extruded FSR */
    FP_PRECISION seg_length_2D = segments_2D[s]._length;
    int extruded_fsr_id = segments_2D[s]._region_id;
    ExtrudedFSR* extruded_FSR = geometry->getExtrudedFSR(extruded_fsr_id);

    /* Determine new mesh and z index */
    if (_global_z_mesh == NULL) {
      num_fsrs = extruded_FSR->_num_fsrs;
      axial_mesh = extruded_FSR->_mesh;
    }

    /* Calculate the end z coordinate of the first track */
    double first_end_z = first_start_z + seg_length_2D / tan_theta;

    /* Find the upper and lower z coordinates of the first track */
    double first_track_lower_z;
    double first_track_upper_z;
    if (sign > 0) {
      first_track_lower_z = first_start_z;
      first_track_upper_z = first_end_z;
    }
    else {
      first_track_lower_z = first_end_z;
      first_track_upper_z = first_start_z;
    }

    /* Loop over all 3D FSRs in the Extruded FSR to find intersections */
    double first_seg_len_3D;
    for (int z_iter = 0; z_iter < num_fsrs; z_iter++) {

      /* If traveling in negative-z direction, loop through FSRs from top */
      int z_ind = z_iter;
      if (sign < 0)
        z_ind = num_fsrs - z_iter - 1;

      /* Extract the FSR ID and Material ID of this 3D FSR */
      int fsr_id = extruded_FSR->_fsr_ids[z_ind];
      Material* material = extruded_FSR->_materials[z_ind];

      /* Find CMFD cell if necessary */
      int cmfd_cell;
      if (cmfd != NULL)
        cmfd_cell = geometry->getCmfdCell(fsr_id);

      /* Get boundaries of the current mesh cell */
      double z_min = axial_mesh[z_ind];
      double z_max = axial_mesh[z_ind+1];

      /* Calculate z-stack track indexes that cross the 3D FSR */
      int start_track = std::ceil((z_min - first_track_upper_z) / z_spacing);
      int start_full = std::ceil((z_min - first_track_lower_z) / z_spacing);
      int end_full = std::ceil((z_max - first_track_upper_z) / z_spacing);
      int end_track = std::ceil((z_max - first_track_lower_z) / z_spacing);

      /* Check track bounds */
      start_track = std::max(start_track, 0);
      end_track = std::min(end_track, num_z_stack);

      /* Treat lower tracks that do not cross the entire 2D length */
      int min_lower = std::min(start_full, end_full);
      first_seg_len_3D = (first_track_upper_z - z_min) / std::abs(cos_theta);
      for (int i = start_track; i < min_lower; i++) {

        /* Calculate distance traveled in 3D FSR */
        double seg_len_3D = first_seg_len_3D + i * track_spacing_3D;

        /* Determine if segment length is large enough to operate on */
        if (seg_len_3D > TINY_MOVE) {

          /* Initialize CMFD surfaces to none (-1) */
          int cmfd_surface_fwd = -1;
          int cmfd_surface_bwd = -1;

          /* Get CMFD surface if necessary */
          if (cmfd != NULL) {
            double start_z = first_track_lower_z + i * z_spacing;
            double end_z = first_track_upper_z + i * z_spacing;
            double dist_to_corner = std::abs((z_min - start_z) / cos_theta);
            if (sign > 0) {
              cmfd_surface_fwd = segments_2D[s]._cmfd_surface_fwd;
              cmfd_surface_fwd = cmfd->findCmfdSurfaceOTF(cmfd_cell, end_z,
                                                          cmfd_surface_fwd);
              if (dist_to_corner <= TINY_MOVE)
                cmfd_surface_bwd = segments_2D[s]._cmfd_surface_bwd;
              cmfd_surface_bwd = cmfd->findCmfdSurfaceOTF(cmfd_cell, z_min,
                                                          cmfd_surface_bwd);
            }
            else {
              if (dist_to_corner <= TINY_MOVE)
                cmfd_surface_fwd = segments_2D[s]._cmfd_surface_fwd;
              cmfd_surface_fwd = cmfd->findCmfdSurfaceOTF(cmfd_cell, z_min,
                                                          cmfd_surface_fwd);
              cmfd_surface_bwd = segments_2D[s]._cmfd_surface_bwd;
              cmfd_surface_bwd = cmfd->findCmfdSurfaceOTF(cmfd_cell, end_z,
                                                          cmfd_surface_bwd);
            }
          }

          /* Operate on segment */
          kernels[0]->execute(seg_len_3D, material, fsr_id, i,
                              cmfd_surface_fwd, cmfd_surface_bwd);
        }
      }

      /* Find if there are tracks the traverse the entire 2D length */
      if (end_full > start_full) {

        /* Calculate distance traveled in 3D FSR */
        double seg_len_3D = seg_length_2D / sin_theta;

        /* Determine if segment length is large enough to operate on */
        if (seg_len_3D > TINY_MOVE) {

          /* Treat tracks that do cross the entire 2D length */
          for (int i = start_full; i < end_full; i++) {

            /* Initialize CMFD surfaces to 2D CMFD surfaces */
            int cmfd_surface_fwd = segments_2D[s]._cmfd_surface_fwd;
            int cmfd_surface_bwd = segments_2D[s]._cmfd_surface_bwd;

            /* Get CMFD surfaces if necessary */
            if (cmfd != NULL) {

              /* Calculate start and end z */
              double start_z = first_start_z + i * z_spacing;
              double end_z = first_end_z + i * z_spacing;
              cmfd_surface_fwd = cmfd->findCmfdSurfaceOTF(cmfd_cell, end_z,
                                                          cmfd_surface_fwd);
              cmfd_surface_bwd = cmfd->findCmfdSurfaceOTF(cmfd_cell, start_z,
                                                          cmfd_surface_bwd);
            }

            /* Operate on segment */
            kernels[0]->execute(seg_len_3D, material, fsr_id, i,
                                cmfd_surface_fwd, cmfd_surface_bwd);
          }
        }
      }

      /* Find if there are tracks that cross both upper and lower boundaries
         NOTE: this will only be true if there are no tracks that cross the
         entire 2D length in the FSR */
      else if (start_full > end_full) {

        /* Calculate distance traveled in 3D FSR */
        double seg_len_3D = (z_max - z_min) / std::abs(cos_theta);

        /* Determine if segment length is large enough to operate on */
        if (seg_len_3D > TINY_MOVE) {

          /* Treat tracks that cross through both the upper and lower axial
             boundaries */
          for (int i = end_full; i < start_full; i++) {

            /* Initialize CMFD surfaces to none (-1) */
            int cmfd_surface_bwd = -1;
            int cmfd_surface_fwd = -1;

            /* Get CMFD surfaces if necessary */
            if (cmfd != NULL) {

              /* Determine start and end z */
              double enter_z;
              double exit_z;
              if (sign > 0) {
                enter_z = z_min;
                exit_z = z_max;
              }
              else {
                enter_z = z_max;
                exit_z = z_min;
              }

              /* Determine if any corners in the s-z plane are hit */
              double dist_to_corner;
              double track_end_z = first_end_z + i * z_spacing;
              dist_to_corner = (track_end_z - exit_z) / cos_theta;
              if (dist_to_corner <= TINY_MOVE)
                cmfd_surface_fwd = segments_2D[s]._cmfd_surface_fwd;

              double track_start_z = first_start_z + i * z_spacing;
              dist_to_corner = (enter_z - track_start_z) / cos_theta;
              if (dist_to_corner <= TINY_MOVE)
                cmfd_surface_bwd = segments_2D[s]._cmfd_surface_bwd;

              /* Find CMFD surfaces */
              cmfd_surface_fwd = cmfd->findCmfdSurfaceOTF(cmfd_cell, exit_z,
                                                          cmfd_surface_fwd);
              cmfd_surface_bwd = cmfd->findCmfdSurfaceOTF(cmfd_cell, enter_z,
                                                          cmfd_surface_bwd);
            }

            /* Operate on segment */
            kernels[0]->execute(seg_len_3D, material, fsr_id, i,
                                cmfd_surface_fwd, cmfd_surface_bwd);
          }
        }
      }

      /* Treat upper tracks that do not cross the entire 2D length */
      int min_upper = std::max(start_full, end_full);
      first_seg_len_3D = (z_max - first_track_lower_z) / std::abs(cos_theta);
      for (int i = min_upper; i < end_track; i++) {

        /* Calculate distance traveled in 3D FSR */
        double seg_len_3D = first_seg_len_3D - i * track_spacing_3D;

        /* Determine if segment length is large enough to operate on */
        if (seg_len_3D > TINY_MOVE) {

          /* Initialize CMFD surfaces to none (-1) */
          int cmfd_surface_fwd = -1;
          int cmfd_surface_bwd = -1;

          /* Get CMFD surface if necessary */
          if (cmfd != NULL) {
            double start_z = first_track_lower_z + i * z_spacing;
            double end_z = first_track_upper_z + i * z_spacing;
            double dist_to_corner = (end_z - z_max) / std::abs(cos_theta);
            if (sign > 0) {
              if (dist_to_corner <= TINY_MOVE)
                cmfd_surface_fwd = segments_2D[s]._cmfd_surface_fwd;
              cmfd_surface_fwd = cmfd->findCmfdSurfaceOTF(cmfd_cell, z_max,
                                                          cmfd_surface_fwd);
              cmfd_surface_bwd = segments_2D[s]._cmfd_surface_bwd;
              cmfd_surface_bwd = cmfd->findCmfdSurfaceOTF(cmfd_cell, start_z,
                                                          cmfd_surface_bwd);
            }
            else {
              cmfd_surface_fwd = segments_2D[s]._cmfd_surface_fwd;
              cmfd_surface_fwd = cmfd->findCmfdSurfaceOTF(cmfd_cell, start_z,
                                                          cmfd_surface_fwd);
              if (dist_to_corner <= TINY_MOVE)
                cmfd_surface_bwd = segments_2D[s]._cmfd_surface_bwd;
              cmfd_surface_bwd = cmfd->findCmfdSurfaceOTF(cmfd_cell, z_max,
                                                          cmfd_surface_bwd);
            }
          }

          /* Operate on segment */
          kernels[0]->execute(seg_len_3D, material, fsr_id, i,
                              cmfd_surface_fwd, cmfd_surface_bwd);
        }
      }
    }
    /* Traverse segment on first track */
    first_start_z = first_end_z;
  }
}


/**
 * @brief A function that searches for the index into a values mesh using a
 *        binary search.
 * @details A binary search is used to calculate the index into a mesh of where
 *          the value val resides. If a mesh boundary is hit, the upper region
 *          is selected for positive-z traversing rays and the lower region is
 *          selected for negative-z traversing rays.
 * @param values an array of monotonically increasing values
 * @param size the size of the values array
 * @param val the level to be searched for in the mesh
 * @param sign the direction of the ray in the z-direction
 */
int TraverseSegments::findMeshIndex(FP_PRECISION* values, int size,
                                 FP_PRECISION val, int sign) {

  /* Initialize indexes into the values array */
  int imin = 0;
  int imax = size-1;

  /* Check if val is outside the range */
  if (val < values[imin] or val > values[imax]) {
    log_printf(ERROR, "Value out of the mesh range in binary search");
    return -1;
  }

  /* Search for interval containing val */
  while (imax - imin > 1) {

    int imid = (imin + imax) / 2;

    if (val > values[imid])
      imin = imid;
    else if (val < values[imid])
      imax = imid;
    else {
      if (sign > 0)
        return imid;
      else
        return imid-1;
    }
  }
  return imin;
}


//FIXME
void TraverseSegments::loopOverTracksByStackTwoWay(TransportKernel* kernel) {

  if (_segment_formation != OTF_STACKS)
    log_printf(ERROR, "Two way on-the-fly transport has only been implemented "
                      "for ray tracing by z-stack");

  int num_2D_tracks = _track_generator_3D->getNum2DTracks();
  Track** flattened_tracks = _track_generator_3D->get2DTracksArray();
  Track3D**** tracks_3D = _track_generator_3D->get3DTracks();
  int*** tracks_per_stack = _track_generator_3D->getTracksPerStack();
  int num_polar = _track_generator_3D->getNumPolar();
  int tid = omp_get_thread_num();

#pragma omp for
  /* Loop over flattened 2D tracks */
  for (int ext_id=0; ext_id < num_2D_tracks; ext_id++) {

    /* Extract indices of 3D tracks associated with the flattened track */
    Track* flattened_track = flattened_tracks[ext_id];
    int a = flattened_track->getAzimIndex();
    int i = flattened_track->getXYIndex();

    /* Loop over polar angles */
    for (int p=0; p < num_polar; p++) {

      /* Trace all tracks in the z-stack if necessary */
      Track* track_3D = &tracks_3D[a][i][p][0];
      if (kernel != NULL) {

        /* Reset kernels to their new Track */
        kernel->newTrack(track_3D);

        /* Trace all segments in the z-stack */
        traceStackTwoWay(flattened_track, p, kernel);
        track_3D->setNumSegments(kernel->getCount());
      }

      /* Operate on the Track */
      segment* segments = _track_generator_3D->getTemporarySegments(tid, 0);
      onTrack(track_3D, segments);
    }
  }
}


/**
 * @brief Traces the 3D segments of 3D Tracks in a z-stack both forward and
 *        backward across the geometry, applying the kernels provided to the
 *        user when the segment information is calcuated.
 * @details This function copies information of the 3D z-stack, ray traces the
 *          z-stack forward using TrackGenerator::traceStackOTF, then reverses
 *          the tracks so that they point backwards, and ray traces in the
 *          reverse direction. This allows segments to be applied to
 *          TransportKernels during the on-the-fly ray tracing process.
 * @param flattened_track the 2D track associated with the z-stack for which
 *        3D segments are computed
 * @param polar_index the polar index of the 3D Track z-stack
 * @param kernels An array of MOCKernel pointers of size greater than or equal
 *        to the number of 3D Tracks in the z-stack which are applied to the
 *        calculated 3D segments
 */
//FIXME
void TraverseSegments::traceStackTwoWay(Track* flattened_track, int polar_index,
                                        TransportKernel* kernel) {

  /* Copy segments from flattened track */
  segment* segments = flattened_track->getSegments();
  Track3D**** tracks_3D = _track_generator_3D->get3DTracks();
  int*** tracks_per_stack = _track_generator_3D->getTracksPerStack();
  MOCKernel* moc_kernel = dynamic_cast<MOCKernel*>(kernel);

  /* Get the first track in the 3D track stack */
  int azim_index = flattened_track->getAzimIndex();
  int track_index = flattened_track->getXYIndex();
  Track3D* first = &tracks_3D[azim_index][track_index][polar_index][0];
  int num_z_stack = tracks_per_stack[azim_index][track_index][polar_index];

  /* Copy spatial data from track stack */
  double start_2D[3], end_2D[3], start_3D[3], end_3D[3];
  for (int i = 0; i < 3; i++) {
    start_2D[i] = flattened_track->getStart()->getXYZ()[i];
    end_2D[i] = flattened_track->getEnd()->getXYZ()[i];
    start_3D[i] = first->getStart()->getXYZ()[i];
    end_3D[i] = first->getEnd()->getXYZ()[i];
  }

  /* Copy directional data from track stack */
  double phi = flattened_track->getPhi();
  double theta = first->getTheta();

  /* Trace stack forwards */
  kernel->setDirection(true);
  traceStackOTF(flattened_track, polar_index, &moc_kernel);
  kernel->post();

  /* Reflect track stack */
  first->getStart()->setXYZ(end_3D);
  first->getEnd()->setXYZ(start_3D);
  first->setTheta(M_PI - theta);
  flattened_track->getStart()->setXYZ(end_2D);
  flattened_track->getEnd()->setXYZ(start_2D);
  flattened_track->setPhi(M_PI + phi);

  /* Reverse segments in flattened track */
  int num_segments = flattened_track->getNumSegments();
  for (int s = 0; s < num_segments/2; s++) {
    segment tmp_segment = segments[num_segments-s-1];
    segments[num_segments-s-1] = segments[s];
    segments[s] = tmp_segment;
  }

  /* Flip CMFD surfaces on segments in flattened track */
  for (int s = 0; s < num_segments; s++) {
    int tmp_surface = segments[s]._cmfd_surface_fwd;
    segments[s]._cmfd_surface_fwd = segments[s]._cmfd_surface_bwd;
    segments[s]._cmfd_surface_bwd = tmp_surface;
  }

  /* Trace stack backwards */
  kernel->setDirection(false);
  traceStackOTF(flattened_track, polar_index, &moc_kernel);
  kernel->post();

  /* Reflect track stack back to forwards */
  first->getStart()->setXYZ(start_3D);
  first->getEnd()->setXYZ(end_3D);
  first->setTheta(theta);
  flattened_track->getStart()->setXYZ(start_2D);
  flattened_track->getEnd()->setXYZ(end_2D);
  flattened_track->setPhi(phi);

  /* Reverse segments in flattened track */
  for (int s = 0; s < num_segments/2; s++) {
    segment tmp_segment = segments[num_segments-s-1];
    segments[num_segments-s-1] = segments[s];
    segments[s] = tmp_segment;
  }

  /* Flip CMFD surfaces on segments in flattened track */
  for (int s = 0; s < num_segments; s++) {
    int tmp_surface = segments[s]._cmfd_surface_fwd;
    segments[s]._cmfd_surface_fwd = segments[s]._cmfd_surface_bwd;
    segments[s]._cmfd_surface_bwd = tmp_surface;
  }
}
