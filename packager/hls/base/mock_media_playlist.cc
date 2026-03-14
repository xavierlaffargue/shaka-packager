// Copyright 2016 Google LLC. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <packager/hls/base/mock_media_playlist.h>

#include <packager/hls_params.h>

namespace shaka {
namespace hls {

MockMediaPlaylist::MockMediaPlaylist(const std::string& file_name,
                                     const std::string& name,
                                     const std::string& group_id)
    : MediaPlaylist(HlsParams(), file_name, name, group_id) {
  using ::testing::Invoke;
  using ::testing::Return;

  ON_CALL(*this, GetMediaInfo()).WillByDefault(Invoke([this]() -> MediaInfo {
    return this->MediaPlaylist::GetMediaInfo();
  }));
  ON_CALL(*this, codec()).WillByDefault(Invoke([this]() -> const std::string& {
    return this->MediaPlaylist::codec();
  }));
  ON_CALL(*this, language()).WillByDefault(Invoke([this]() -> const std::string& {
    return this->MediaPlaylist::language();
  }));
  ON_CALL(*this, characteristics())
      .WillByDefault(Invoke([this]() -> const std::vector<std::string>& {
        return this->MediaPlaylist::characteristics();
      }));
  ON_CALL(*this, is_dvs()).WillByDefault(Invoke([this]() -> bool {
    return this->MediaPlaylist::is_dvs();
  }));
}
MockMediaPlaylist::~MockMediaPlaylist() {}

}  // namespace hls
}  // namespace shaka
