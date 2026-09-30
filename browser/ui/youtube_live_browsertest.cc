// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service_factory.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"

namespace yee {
namespace {

// This opt-in experiment records live delivery. An absent control ad is
// inconclusive, rather than a passing ad-blocking assertion.
class YouTubeLiveBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override { host_resolver()->AllowDirectLookup("*"); }
};

IN_PROC_BROWSER_TEST_F(YouTubeLiveBrowserTest, DISABLED_MidrollObservation) {
  const auto* command_line = base::CommandLine::ForCurrentProcess();
  const std::string mode =
      command_line->GetSwitchValueASCII("yee-live-youtube-mode");
  ASSERT_TRUE(mode == "on" || mode == "off");
  const bool enabled = mode == "on";
  const bool seek = command_line->HasSwitch("yee-live-youtube-seek");
  const std::string selected_video =
      command_line->GetSwitchValueASCII("yee-live-youtube-video");
  ASSERT_TRUE(selected_video.empty() || selected_video == "5EzB_2Qcakw" ||
              selected_video == "uq14seOjILU" ||
              selected_video == "mfmdXPT7nAM");
  int seconds = 180;
  const std::string seconds_arg =
      command_line->GetSwitchValueASCII("yee-live-youtube-seconds");
  if (!seconds_arg.empty()) {
    ASSERT_TRUE(base::StringToInt(seconds_arg, &seconds));
  }
  ASSERT_GE(seconds, 60);
  ASSERT_LE(seconds, 600);
  const auto report_path =
      command_line->GetSwitchValuePath("yee-live-youtube-report");
  ASSERT_FALSE(report_path.empty());
  auto* service =
      content_blocking::ContentBlockingServiceFactory::GetForProfile(
          browser()->GetProfile());
  ASSERT_TRUE(service);
  service->SetEnabledForSite(GURL("https://www.youtube.com/"), enabled);
  base::ListValue observations;

  for (const char* video_id : {"5EzB_2Qcakw", "uq14seOjILU", "mfmdXPT7nAM"}) {
    if (!selected_video.empty() && selected_video != video_id) {
      continue;
    }
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(),
        GURL(std::string("https://www.youtube.com/watch?v=") + video_id)));
    auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(
        content::ExecJs(contents, content::JsReplace(R"(
      (() => {
        const wanted = $1, seconds = $2, seek = $3;
        const started = Date.now();
        const result = {videoId: wanted, startedAt: new Date().toISOString(),
          samples: 0, contentSeconds: 0, preRoll: false, midRoll: false,
          adAudioDecoded: false, contentAudioDecoded: false,
          seeks: [], events: [], done: false};
        window.__yeeLiveObservation = result;
        let previousTime = null, previousAudio = null, lastSignature = '',
            stalled = 0, seekIndex = 0;
        const timer = setInterval(() => {
          const video = document.querySelector('video');
          const player = document.getElementById('movie_player');
          const elapsed = (Date.now() - started) / 1000;
          const errorElement = document.querySelector('.ytp-error-content-wrap');
          const error = errorElement?.offsetParent !== null ? errorElement : null;
          const ad = !!player && (player.classList.contains('ad-showing') ||
                                     player.classList.contains('ad-interrupting'));
          const time = video?.currentTime ?? null;
          const duration = video && Number.isFinite(video.duration) ? video.duration : null;
          const audio = video?.webkitAudioDecodedByteCount ?? null;
          const state = {elapsed: Math.round(elapsed), ad, time, duration,
            paused: video?.paused ?? true, readyState: video?.readyState ?? 0,
            muted: video?.muted ?? null, audioDecodedBytes: audio,
            error: video?.error?.code ?? null};
          ++result.samples;
          if (ad) {
            if (result.contentSeconds >= 20) result.midRoll = true;
            else result.preRoll = true;
          }
          if (video && !video.paused && previousTime !== null) {
            const delta = time - previousTime;
            if (!ad && delta > 0 && delta < 3) result.contentSeconds += delta;
            stalled = !ad && delta < 0.05 ? stalled + 1 : 0;
            if (audio !== null && previousAudio !== null && audio > previousAudio) {
              if (ad) result.adAudioDecoded = true;
              else result.contentAudioDecoded = true;
            }
          }
          previousTime = time;
          previousAudio = audio;
          const signature = JSON.stringify([ad, state.paused, state.readyState,
            state.error, !!error, stalled > 15]);
          if (signature !== lastSignature || result.samples % 20 === 0) {
            result.events.push(state); lastSignature = signature;
          }
          if (video) {
            video.muted = false; video.volume = 0.1;
            if (video.paused && !video.ended) {
              video.play().catch(e => {result.playError = e.name;});
            }
            if (seek && !ad && duration && result.contentSeconds >= (seekIndex + 1) * 40 && seekIndex < 2) {
              const target = duration * (seekIndex === 0 ? 0.35 : 0.7);
              result.seeks.push({elapsed, target}); ++seekIndex;
              if (player?.seekTo) player.seekTo(target, true);
              else video.currentTime = target;
              previousTime = null;
            }
          }
          if (elapsed >= seconds || state.error || (error && elapsed > 20) ||
              (stalled > 30 && elapsed > 40)) {
            result.error = state.error || (error ? error.textContent?.trim().slice(0, 180) : null);
            result.stalled = stalled > 30;
            result.last = state;
            result.done = true; clearInterval(timer);
          }
        }, 1000);
      })();
    )",
                                                     video_id, seconds, seek)));
    bool done = false;
    for (int tick = 0; tick < seconds / 20 + 3; ++tick) {
      done = content::EvalJs(contents, R"(
        new Promise(resolve => {
          let attempts = 0;
          const poll = setInterval(() => {
            if (window.__yeeLiveObservation.done || ++attempts >= 20) {
              clearInterval(poll); resolve(window.__yeeLiveObservation.done);
            }
          }, 1000);
        })
      )")
                 .ExtractBool();
      if (done) {
        break;
      }
    }
    EXPECT_TRUE(done);
    const auto result =
        content::EvalJs(contents, "JSON.stringify(window.__yeeLiveObservation)")
            .ExtractString();
    auto observation = base::JSONReader::Read(result, base::JSON_PARSE_RFC);
    ASSERT_TRUE(observation && observation->is_dict());
    observations.Append(std::move(*observation));
    base::DictValue report;
    report.Set("protection", enabled);
    report.Set("secondsPerVideo", seconds);
    report.Set("seek", seek);
    report.Set("observations", observations.Clone());
    base::ScopedAllowBlockingForTesting allow_report_write;
    ASSERT_TRUE(
        base::WriteFile(report_path, base::WriteJson(report).value_or("{}")));
  }
}

}  // namespace
}  // namespace yee
