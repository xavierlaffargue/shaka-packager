// Copyright 2016 Google LLC. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file at
// https://developers.google.com/open-source/licenses/bsd

#include <packager/hls/base/master_playlist.h>

#include <algorithm>  // std::max
#include <cstdint>
#include <filesystem>
#include <set>
#include <vector>

#include <absl/log/check.h>
#include <absl/log/log.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_format.h>
#include <absl/strings/str_join.h>

#include <packager/file.h>
#include <packager/hls/base/media_playlist.h>
#include <packager/hls/base/tag.h>
#include <packager/macros/logging.h>
#include <packager/version/version.h>

#include "packager/kv_pairs/kv_pairs.h"
#include "packager/utils/string_trim_split.h"

namespace shaka {
namespace hls {
namespace {
const char* kDefaultAudioGroupId = "default-audio-group";
const char* kDefaultSubtitleGroupId = "default-text-group";
const char* kUnexpectedGroupId = "unexpected-group";

void AppendVersionString(std::string* content) {
  const std::string version = GetPackagerVersion();
  if (version.empty())
    return;
  absl::StrAppendFormat(content, "## Generated with %s version %s\n",
                        GetPackagerProjectUrl().c_str(), version.c_str());
}

// This structure roughly maps to the Variant stream in HLS specification.
// Each variant specifies zero or one audio group and zero or one text group.
struct Variant {
  std::set<std::string> audio_codecs;
  std::set<std::string> text_codecs;
  const std::string* audio_group_id = nullptr;
  const std::string* text_group_id = nullptr;
  bool have_instream_closed_caption = false;
  // The bitrates should be the sum of audio bitrate and text bitrate.
  // However, given the constraints and assumptions, it makes sense to exclude
  // text bitrate out of the calculation:
  // - Text streams usually have a very small negligible bitrate.
  // - Text does not have constant bitrates. To avoid fluctuation, an arbitrary
  //   value is assigned to the text bitrates in the parser. It does not make
  //   sense to take that text bitrate into account here.
  uint64_t max_audio_bitrate = 0;
  uint64_t avg_audio_bitrate = 0;
  uint32_t min_index = 0xFFFFFFFF;
};

uint64_t GetMaximumMaxBitrate(const std::list<const MediaPlaylist*> playlists) {
  uint64_t max = 0;
  for (const auto& playlist : playlists) {
    max = std::max(max, playlist->MaxBitrate());
  }
  return max;
}

uint64_t GetMaximumAvgBitrate(const std::list<const MediaPlaylist*> playlists) {
  uint64_t max = 0;
  for (const auto& playlist : playlists) {
    max = std::max(max, playlist->AvgBitrate());
  }
  return max;
}

std::set<std::string> GetGroupCodecString(
    const std::list<const MediaPlaylist*>& group) {
  std::set<std::string> codecs;

  for (const MediaPlaylist* playlist : group) {
    codecs.insert(playlist->codec());
  }

  // To support some older players, we cannot include "wvtt" in the codec
  // string. As per HLS guidelines, "wvtt" is optional. When it is included, it
  // can cause playback errors on some Apple produces. Excluding it allows
  // playback on all Apple products. See
  // https://github.com/shaka-project/shaka-packager/issues/402 for all details.
  auto wvtt = codecs.find("wvtt");
  if (wvtt != codecs.end()) {
    codecs.erase(wvtt);
  }
  // TTML is specified using 'stpp.ttml.im1t'; see section 5.10 of
  // https://developer.apple.com/documentation/http_live_streaming/hls_authoring_specification_for_apple_devices
  auto ttml = codecs.find("ttml");
  if (ttml != codecs.end()) {
    codecs.erase(ttml);
    codecs.insert("stpp.ttml.im1t");
  }

  return codecs;
}

std::list<Variant> AudioGroupsToVariants(
    const std::list<std::pair<std::string, std::list<const MediaPlaylist*>>>&
        groups) {
  std::list<Variant> variants;

  for (const auto& group : groups) {
    Variant variant;
    variant.audio_group_id = &group.first;
    variant.max_audio_bitrate = GetMaximumMaxBitrate(group.second);
    variant.avg_audio_bitrate = GetMaximumAvgBitrate(group.second);
    variant.audio_codecs = GetGroupCodecString(group.second);
    if (!group.second.empty()) {
      auto info = group.second.front()->GetMediaInfo();
      if (info.has_index()) {
        variant.min_index = info.index();
      }
    }

    variants.push_back(variant);
  }

  // Make sure we return at least one variant so create a null variant if there
  // are no variants.
  if (variants.empty()) {
    variants.emplace_back();
  }

  return variants;
}

const char* GetGroupId(const MediaPlaylist& playlist) {
  const std::string& group_id = playlist.group_id();

  if (!group_id.empty()) {
    return group_id.c_str();
  }

  switch (playlist.stream_type()) {
    case MediaPlaylist::MediaPlaylistStreamType::kAudio:
      return kDefaultAudioGroupId;

    case MediaPlaylist::MediaPlaylistStreamType::kSubtitle:
      return kDefaultSubtitleGroupId;

    default:
      return kUnexpectedGroupId;
  }
}

std::list<Variant> SubtitleGroupsToVariants(
    const std::list<std::pair<std::string, std::list<const MediaPlaylist*>>>&
        groups) {
  std::list<Variant> variants;

  for (const auto& group : groups) {
    Variant variant;
    variant.text_group_id = &group.first;
    variant.text_codecs = GetGroupCodecString(group.second);
    if (!group.second.empty()) {
      auto info = group.second.front()->GetMediaInfo();
      if (info.has_index()) {
        variant.min_index = info.index();
      }
    }

    variants.push_back(variant);
  }

  // Make sure we return at least one variant so create a null variant if there
  // are no variants.
  if (variants.empty()) {
    variants.emplace_back();
  }

  return variants;
}

std::list<Variant> BuildVariants(
    const std::list<std::pair<std::string, std::list<const MediaPlaylist*>>>&
        audio_groups,
    const std::list<std::pair<std::string, std::list<const MediaPlaylist*>>>&
        subtitle_groups,
    const bool have_instream_closed_caption) {
  std::list<Variant> audio_variants = AudioGroupsToVariants(audio_groups);
  std::list<Variant> subtitle_variants =
      SubtitleGroupsToVariants(subtitle_groups);

  DCHECK_GE(audio_variants.size(), 1u);
  DCHECK_GE(subtitle_variants.size(), 1u);

  std::list<Variant> merged;

  for (const auto& audio_variant : audio_variants) {
    for (const auto& subtitle_variant : subtitle_variants) {
      Variant base_variant;
      base_variant.audio_codecs = audio_variant.audio_codecs;
      base_variant.text_codecs = subtitle_variant.text_codecs;
      base_variant.audio_group_id = audio_variant.audio_group_id;
      base_variant.text_group_id = subtitle_variant.text_group_id;
      base_variant.max_audio_bitrate = audio_variant.max_audio_bitrate;
      base_variant.avg_audio_bitrate = audio_variant.avg_audio_bitrate;
      base_variant.have_instream_closed_caption = have_instream_closed_caption;
      base_variant.min_index =
          std::min(audio_variant.min_index, subtitle_variant.min_index);
      merged.push_back(base_variant);
    }
  }

  DCHECK_GE(merged.size(), 1u);

  return merged;
}

void BuildStreamInfTag(const MediaPlaylist& playlist,
                       const Variant& variant,
                       const std::string& base_url,
                       std::string* out) {
  DCHECK(out);

  std::string tag_name;
  switch (playlist.stream_type()) {
    case MediaPlaylist::MediaPlaylistStreamType::kAudio:
    case MediaPlaylist::MediaPlaylistStreamType::kVideo:
      tag_name = "#EXT-X-STREAM-INF";
      break;
    case MediaPlaylist::MediaPlaylistStreamType::kVideoIFramesOnly:
      tag_name = "#EXT-X-I-FRAME-STREAM-INF";
      break;
    default:
      NOTIMPLEMENTED() << "Cannot build STREAM-INFO tag for type "
                       << static_cast<int>(playlist.stream_type());
      break;
  }
  Tag tag(tag_name, out);

  tag.AddNumber("BANDWIDTH", playlist.MaxBitrate() + variant.max_audio_bitrate);
  tag.AddNumber("AVERAGE-BANDWIDTH",
                playlist.AvgBitrate() + variant.avg_audio_bitrate);

  std::vector<std::string> all_codecs;
  all_codecs.push_back(playlist.codec());
  all_codecs.insert(all_codecs.end(), variant.audio_codecs.begin(),
                    variant.audio_codecs.end());
  all_codecs.insert(all_codecs.end(), variant.text_codecs.begin(),
                    variant.text_codecs.end());
  tag.AddQuotedString("CODECS", absl::StrJoin(all_codecs, ","));

  if (playlist.supplemental_codec() != "" &&
      playlist.compatible_brand() != media::FOURCC_NULL) {
    std::vector<std::string> supplemental_codecs;
    supplemental_codecs.push_back(playlist.supplemental_codec());
    supplemental_codecs.push_back(FourCCToString(playlist.compatible_brand()));
    tag.AddQuotedString("SUPPLEMENTAL-CODECS",
                        absl::StrJoin(supplemental_codecs, "/"));
  }

  uint32_t width;
  uint32_t height;

  const bool is_iframe_playlist =
      playlist.stream_type() ==
      MediaPlaylist::MediaPlaylistStreamType::kVideoIFramesOnly;

  if (playlist.GetDisplayResolution(&width, &height)) {
    tag.AddNumberPair("RESOLUTION", width, 'x', height);

    // Right now the frame-rate returned may not be accurate in some scenarios.
    // TODO(kqyang): Fix frame-rate computation.
    if (!is_iframe_playlist) {
      const double frame_rate = playlist.GetFrameRate();
      if (frame_rate > 0)
        tag.AddFloat("FRAME-RATE", frame_rate);
    }

    const std::string video_range = playlist.GetVideoRange();
    if (!video_range.empty())
      tag.AddString("VIDEO-RANGE", video_range);
  }

  if (!is_iframe_playlist) {
    if (variant.audio_group_id) {
      tag.AddQuotedString("AUDIO", *variant.audio_group_id);
    }

    if (variant.text_group_id) {
      tag.AddQuotedString("SUBTITLES", *variant.text_group_id);
    }

    if (variant.have_instream_closed_caption) {
      tag.AddQuotedString("CLOSED-CAPTIONS", "CC");
    } else {
      tag.AddString("CLOSED-CAPTIONS", "NONE");
    }
  }

  if (is_iframe_playlist) {
    tag.AddQuotedString("URI", base_url + playlist.file_name());
    out->append("\n");
  } else {
    absl::StrAppendFormat(out, "\n%s%s\n", base_url.c_str(),
                          playlist.file_name().c_str());
  }
}

// Need to pass in |group_id| as it may have changed to a new default when
// grouped with other playlists.
void BuildMediaTag(const MediaPlaylist& playlist,
                   const std::string& group_id,
                   bool is_default,
                   bool is_autoselect,
                   const std::string& base_url,
                   std::string* out) {
  // Tag attributes should follow the order as defined in
  // https://tools.ietf.org/html/draft-pantos-http-live-streaming-23#section-3.5

  Tag tag("#EXT-X-MEDIA", out);

  // We should only be making media tags for audio and text.
  switch (playlist.stream_type()) {
    case MediaPlaylist::MediaPlaylistStreamType::kAudio:
      tag.AddString("TYPE", "AUDIO");
      break;

    case MediaPlaylist::MediaPlaylistStreamType::kSubtitle:
      tag.AddString("TYPE", "SUBTITLES");
      break;

    default:
      NOTIMPLEMENTED() << "Cannot build media tag for type "
                       << static_cast<int>(playlist.stream_type());
      break;
  }

  tag.AddQuotedString("URI", base_url + playlist.file_name());
  tag.AddQuotedString("GROUP-ID", group_id);

  const std::string& language = playlist.language();
  if (!language.empty()) {
    tag.AddQuotedString("LANGUAGE", language);
  }

  tag.AddQuotedString("NAME", playlist.name());

  if (is_default) {
    tag.AddString("DEFAULT", "YES");
  } else {
    tag.AddString("DEFAULT", "NO");
  }
  if (is_autoselect) {
    tag.AddString("AUTOSELECT", "YES");
  }

  if (playlist.stream_type() ==
          MediaPlaylist::MediaPlaylistStreamType::kSubtitle &&
      playlist.forced_subtitle()) {
    tag.AddString("FORCED", "YES");
  }

  const std::vector<std::string>& characteristics = playlist.characteristics();
  if (!characteristics.empty()) {
    tag.AddQuotedString("CHARACTERISTICS", absl::StrJoin(characteristics, ","));
  }

  const MediaPlaylist::MediaPlaylistStreamType kAudio =
      MediaPlaylist::MediaPlaylistStreamType::kAudio;
  if (playlist.stream_type() == kAudio) {
    if (playlist.GetEC3JocComplexity() != 0) {
      // HLS Authoring Specification for Apple Devices Appendices documents how
      // to handle Dolby Digital Plus JOC content.
      // https://developer.apple.com/documentation/http_live_streaming/hls_authoring_specification_for_apple_devices/hls_authoring_specification_for_apple_devices_appendices
      std::string channel_string =
          std::to_string(playlist.GetEC3JocComplexity()) + "/JOC";
      tag.AddQuotedString("CHANNELS", channel_string);
    } else if (playlist.GetAC4ImsFlag() || playlist.GetAC4CbiFlag()) {
      // Dolby has qualified using IMSA to present AC4 immersive audio (IMS and
      // CBI without object-based audio) for Dolby internal use only. IMSA is
      // not included in any publicly-available specifications as of June, 2020.
      std::string channel_string =
          std::to_string(playlist.GetNumChannels()) + "/IMSA";
      tag.AddQuotedString("CHANNELS", channel_string);
    } else {
      // According to HLS spec:
      // https://tools.ietf.org/html/draft-pantos-hls-rfc8216bis 4.4.6.1.
      // CHANNELS is a quoted-string that specifies an ordered,
      // slash-separated ("/") list of parameters. The first parameter is a
      // count of audio channels, and the second parameter identifies the
      // encoding of object-based audio used by the Rendition.
      std::string channel_string = std::to_string(playlist.GetNumChannels());
      tag.AddQuotedString("CHANNELS", channel_string);
    }
  }
  out->append("\n");
}

void BuildMediaTags(
    std::list<std::pair<std::string, std::list<const MediaPlaylist*>>>& groups,
    const std::string& default_language,
    const std::string& base_url,
    bool has_index,
    std::string* out) {
  for (const auto& group : groups) {
    const std::string& group_id = group.first;
    const auto& playlists = group.second;

    bool group_has_default = false;
    std::set<std::string> languages;

    for (const auto& playlist : playlists) {
      bool is_default = false;
      bool is_autoselect = false;

      const std::string language = playlist->language();
      const bool is_dvs = playlist->is_dvs();

      if (is_dvs) {
        is_autoselect = true;
        if (has_index && !group_has_default && !language.empty() && language == default_language) {
          is_default = true;
          group_has_default = true;
        }
      } else {
        if (languages.find(language) == languages.end()) {
          if (!group_has_default && !language.empty() && language == default_language) {
             is_default = true;
             group_has_default = true;
          }
          is_autoselect = true;
          languages.insert(language);
        }
      }

      if (playlist->stream_type() ==
              MediaPlaylist::MediaPlaylistStreamType::kSubtitle &&
          playlist->forced_subtitle()) {
        is_autoselect = true;
      }

      BuildMediaTag(*playlist, group_id, is_default, is_autoselect, base_url,
                    out);
    }
  }
}

bool ListOrderFn(const MediaPlaylist* a, const MediaPlaylist* b) {
  auto info_a = a->GetMediaInfo();
  auto info_b = b->GetMediaInfo();
  uint32_t a_idx = info_a.has_index() ? info_a.index() : 0xFFFFFFFF;
  uint32_t b_idx = info_b.has_index() ? info_b.index() : 0xFFFFFFFF;
  return a_idx < b_idx;
}

bool GroupOrderFn(const std::pair<std::string, std::list<const MediaPlaylist*>>& a,
                  const std::pair<std::string, std::list<const MediaPlaylist*>>& b) {
  return ListOrderFn(a.second.front(), b.second.front());
}

void BuildCeaMediaTag(const CeaCaption& caption, std::string* out) {
  Tag tag("#EXT-X-MEDIA", out);
  tag.AddString("TYPE", "CLOSED-CAPTIONS");
  tag.AddQuotedString("GROUP-ID", "CC");
  tag.AddQuotedString("NAME", caption.name);
  if (!caption.language.empty()) {
    tag.AddQuotedString("LANGUAGE", caption.language);
  }
  if (caption.is_default)
    tag.AddString("DEFAULT", "YES");
  else
    tag.AddString("DEFAULT", "NO");
  if (caption.autoselect)
    tag.AddString("AUTOSELECT", "YES");
  else
    tag.AddString("AUTOSELECT", "NO");
  tag.AddQuotedString("INSTREAM-ID", caption.channel);
  out->append("\n");
}

void AppendPlaylists(const std::string& default_audio_language,
                     const std::string& default_text_language,
                     const std::vector<CeaCaption>& closed_captions,
                     const std::string& base_url,
                     const std::list<MediaPlaylist*>& playlists,
                     std::string* content) {
  std::map<std::string, std::list<const MediaPlaylist*>> audio_playlist_groups;
  std::map<std::string, std::list<const MediaPlaylist*>>
      subtitle_playlist_groups;
  std::list<const MediaPlaylist*> video_playlists;
  std::list<const MediaPlaylist*> iframe_playlists;

  bool has_index = true;

  for (const MediaPlaylist* playlist : playlists) {
    has_index = has_index && playlist->GetMediaInfo().has_index();

    switch (playlist->stream_type()) {
      case MediaPlaylist::MediaPlaylistStreamType::kAudio:
        audio_playlist_groups[GetGroupId(*playlist)].push_back(playlist);
        break;
      case MediaPlaylist::MediaPlaylistStreamType::kVideo:
        video_playlists.push_back(playlist);
        break;
      case MediaPlaylist::MediaPlaylistStreamType::kVideoIFramesOnly:
        iframe_playlists.push_back(playlist);
        break;
      case MediaPlaylist::MediaPlaylistStreamType::kSubtitle:
        subtitle_playlist_groups[GetGroupId(*playlist)].push_back(playlist);
        break;
      default:
        NOTIMPLEMENTED() << static_cast<int>(playlist->stream_type())
                         << " not handled.";
    }
  }

  // convert the std::map to std::list and reorder it if indexes were provided
  std::list<std::pair<std::string, std::list<const MediaPlaylist*>>>
      audio_groups_list(audio_playlist_groups.begin(),
                        audio_playlist_groups.end());
  std::list<std::pair<std::string, std::list<const MediaPlaylist*>>>
      subtitle_groups_list(subtitle_playlist_groups.begin(),
                           subtitle_playlist_groups.end());
  if (has_index) {
    for (auto& group : audio_groups_list) {
      group.second.sort(ListOrderFn);
    }
    audio_groups_list.sort(GroupOrderFn);
    for (auto& group : subtitle_groups_list) {
      group.second.sort(ListOrderFn);
    }
    subtitle_groups_list.sort(GroupOrderFn);
    video_playlists.sort(ListOrderFn);
    iframe_playlists.sort(ListOrderFn);
  }

  if (!audio_groups_list.empty()) {
    content->append("\n");
    BuildMediaTags(audio_groups_list, default_audio_language, base_url,
                   has_index, content);
  }

  if (!subtitle_groups_list.empty()) {
    content->append("\n");
    BuildMediaTags(subtitle_groups_list, default_text_language, base_url,
                   has_index, content);
  }

  if (!closed_captions.empty()) {
    content->append("\n");
    for (const auto& caption : closed_captions) {
      BuildCeaMediaTag(caption, content);
    }
  }

  std::list<Variant> variants =
      BuildVariants(audio_groups_list, subtitle_groups_list,
                    !closed_captions.empty());

  if (!video_playlists.empty()) {
    if (has_index) {
      struct FullVariant {
        const MediaPlaylist* video;
        const Variant* variant;
        uint32_t min_index;
      };
      std::vector<FullVariant> full_variants;
      for (const auto& variant : variants) {
        for (const auto& video : video_playlists) {
          auto info = video->GetMediaInfo();
          uint32_t min_idx = info.index();
          if (variant.min_index < min_idx) {
            min_idx = variant.min_index;
          }
          full_variants.push_back({video, &variant, min_idx});
        }
      }
      std::stable_sort(
          full_variants.begin(), full_variants.end(),
          [](const FullVariant& a, const FullVariant& b) {
            return a.min_index < b.min_index;
          });
      for (const auto& fv : full_variants) {
        content->append("\n");
        BuildStreamInfTag(*fv.video, *fv.variant, base_url, content);
      }
    } else {
      for (const auto& variant : variants) {
        content->append("\n");
        for (const auto& playlist : video_playlists) {
          BuildStreamInfTag(*playlist, variant, base_url, content);
        }
      }
    }
  }

  if (!iframe_playlists.empty()) {
    content->append("\n");
    for (const auto& playlist : iframe_playlists) {
      // I-Frame playlists do not have variant. Just use the default.
      BuildStreamInfTag(*playlist, Variant(), base_url, content);
    }
  }

  // Generate audio-only master playlist when there are no videos and subtitles.
  if (!audio_playlist_groups.empty() && video_playlists.empty() &&
      subtitle_playlist_groups.empty()) {
    content->append("\n");
    for (const auto& playlist_group : audio_groups_list) {
      Variant variant;
      // Populate |audio_group_id|, which will be propagated to "AUDIO" field.
      // Leaving other fields, e.g. xxx_audio_bitrate in |Variant|, as
      // null/empty/zero intentionally as the information is already available
      // in audio |playlist|.
      variant.audio_group_id = &playlist_group.first;
      for (const auto& playlist : playlist_group.second) {
        BuildStreamInfTag(*playlist, variant, base_url, content);
      }
    }
  }
}

}  // namespace

MasterPlaylist::MasterPlaylist(const std::filesystem::path& file_name,
                               const std::string& default_audio_language,
                               const std::string& default_text_language,
                               const std::vector<CeaCaption>& closed_captions,
                               bool is_independent_segments,
                               bool create_session_keys)
    : file_name_(file_name),
      default_audio_language_(default_audio_language),
      default_text_language_(default_text_language),
      closed_captions_(closed_captions),
      is_independent_segments_(is_independent_segments),
      create_session_keys_(create_session_keys) {}

MasterPlaylist::~MasterPlaylist() {}

bool MasterPlaylist::WriteMasterPlaylist(
    const std::string& base_url,
    const std::string& output_dir,
    const std::list<MediaPlaylist*>& playlists) {
  std::string content = "#EXTM3U\n";
  AppendVersionString(&content);

  if (is_independent_segments_) {
    content.append("\n#EXT-X-INDEPENDENT-SEGMENTS\n");
  }

  // Iterate over the playlists and add the session keys to the master playlist.
  if (create_session_keys_) {
    std::set<std::string> session_keys;
    for (const auto& playlist : playlists) {
      for (const auto& entry : playlist->entries()) {
        if (entry->type() == HlsEntry::EntryType::kExtKey) {
          auto encryption_entry =
              dynamic_cast<EncryptionInfoEntry*>(entry.get());
          session_keys.emplace(
              encryption_entry->ToString("#EXT-X-SESSION-KEY"));
        }
      }
    }
    // session_keys will now contain all the unique session keys.
    for (const auto& session_key : session_keys)
      content.append(session_key + "\n");
  }

  AppendPlaylists(default_audio_language_, default_text_language_,
                  closed_captions_, base_url, playlists, &content);

  // Skip if the playlist is already written.
  if (content == written_playlist_)
    return true;

  auto file_path = std::filesystem::u8path(output_dir) / file_name_;
  if (!File::WriteFileAtomically(file_path.string().c_str(), content)) {
    LOG(ERROR) << "Failed to write master playlist to: " << file_path.string();
    return false;
  }
  written_playlist_ = content;
  return true;
}

}  // namespace hls
}  // namespace shaka
