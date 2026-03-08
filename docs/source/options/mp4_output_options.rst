MP4 output options
^^^^^^^^^^^^^^^^^^

--mp4_include_pssh_in_stream

    MP4 only: include pssh in the encrypted stream. Default enabled.

--mp4_use_decoding_timestamp_in_timeline

    Deprecated. Do not use.

--generate_sidx_in_media_segments
--nogenerate_sidx_in_media_segments

    Indicates whether to generate 'sidx' box in media segments. Note
    that it is required for DASH on-demand profile (not using segment
    template).

    Default enabled.

--mvex_before_trak

    Android MediaExtractor requires mvex to be written before trak. Set the flag
    to true to comply with the requirement.

--mp4_reset_initial_composition_offset_to_zero

    MP4 only. If it is true, reset the initial composition offset to zero, i.e.
    by assuming that there is a missing EditList. Default enabled.
