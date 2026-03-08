Muxer options
-------------

--temp_dir <path>

    Specify a directory in which to store temporary (intermediate) files. Used
    only if single_segment=true.

--transport_stream_timestamp_offset_ms <milliseconds>

    A positive value, in milliseconds, by which output timestamps are offset to
    compensate for possible negative timestamps in the input. For example,
    timestamps from ISO-BMFF after adjusted by EditList could be negative. In
    transport streams, timestamps are not allowed to be less than zero.
    Default: 100.

--default_text_zero_bias_ms <milliseconds>

    A positive value, in milliseconds. It is the threshold used to determine if
    we should assume that the text stream actually starts at time zero. If the
    first sample comes before default_text_zero_bias_ms, then the start will be
    padded as the stream is assumed to start at zero. If the first sample comes
    after default_text_zero_bias_ms then the start of the stream will not be
    padded as we cannot assume the start time of the stream.

--strip_parameter_set_nalus

    When converting from NAL byte stream (AnnexB stream) to NAL unit stream,
    this flag determines whether to strip parameter sets NAL units, i.e.
    SPS/PPS for H264 and SPS/PPS/VPS for H265, from the frames. Note that
    avc1/hvc1 is generated if this flag is enabled; otherwise avc3/hev1 is
    generated. Default enabled.

--ignore_http_output_failures

    Ignore HTTP output failures. Can help recover from live stream upload
    errors.

--output_media_info

    Create a human readable format of MediaInfo. The output file name will be
    the name specified by output flag, suffixed with '.media_info'.
