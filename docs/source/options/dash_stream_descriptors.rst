DASH specific stream descriptor fields
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:dash_accessibilities (accessibilities):

    Optional semicolon separated list of values for DASH Accessibility element.
    The value should be in the format: scheme_id_uri=value, which propagates
    to the Accessibility element in the result DASH manifest. See DASH
    (ISO/IEC 23009-1) specification for details.

:dash_roles (roles):

    optional semicolon separated list of values for DASH Role element. The
    value should be one of: **caption**, **subtitle**, **main**, **alternate**,
    **supplementary**, **commentary**, **dub**, **description**, **sign**,
    **metadata**, **enhanced-audio- intelligibility**, **emergency**,
    **forced-subtitle**, **easyreader**, and **karaoke**.

    See DASH (ISO/IEC 23009-1) specification for details.

:closed_captions:

    Specifies one or more CEA-608 closed caption channels. Multiple channels
    can be provided in a single field, separated by semicolons (;). Each channel
    is defined as a comma-separated list of key-value pairs.

    Supported keys:

    - channel: CC1..CC4, SERVICE1..SERVICE63
    - name: arbitrary name
    - lang: ISO-639-2 language code
    - default: yes|no
    - autoselect: yes|no

    Example::

        closed_captions=channel=CC1,name=English,lang=eng;channel=CC2,name=French,lang=fra
