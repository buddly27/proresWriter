#include "Build/fnBuild.h"
#include "DDImage/DDString.h"
#include "DDImage/Writer.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"

#define INT64_C(c) c ## L
#define UINT64_C(c) c ## UL

extern "C" {
#include <errno.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include "libavutil/imgutils.h"
#include "libavformat/avio.h"
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
}

using namespace DD::Image;

#include <iostream>
#include "proresWriter.h"


//***************************************************************************//
//        ProresWriter - Constructor and Destructor and Knobs management     //
//***************************************************************************//

proresWriter::proresWriter(Write* iop)
  : Writer(iop)
  , codecContext_(NULL)
  , formatContext_(NULL)
  , stream_(NULL)
  , error_(IGNORE_FINISH)
  , fps_(DD::Image::root_real_fps())
  , bitrate_(400000)
  , bitrateTolerance_(4000 * 10000)
  , mbDecision_(FF_MB_DECISION_SIMPLE)
  , profilesProres_(2)
{
  av_log_set_level(AV_LOG_WARNING);
//  av_log_set_level(AV_LOG_DEBUG);
  av_register_all();

  codec_ = avcodec_find_encoder_by_name("prores_ks");
  if (!codec_) {
    iop->critical("Codec Prores not found...");
    return;
  }
}

proresWriter::~proresWriter()
{
  av_free(codecContext_);
}

void proresWriter::knobs(Knob_Callback f)
{
  static const char* profilesProresType[] = {
    "Proxy", "LT", "Standard", "HQ", 0
  };
  Enumeration_knob(f, &profilesProres_, profilesProresType, "profilesProres", "profile");
  Float_knob(f, &fps_, IRange(0.0, 100.0f), "fps");

  BeginClosedGroup(f, "Advanced");

  Int_knob(f, &bitrate_, IRange(0.0, 400000), "bitrate");
  SetFlags(f, Knob::SLIDER | Knob::LOG_SLIDER);
  Int_knob(f, &bitrateTolerance_, IRange(0, 4000 * 10000), "bitrateTol", "bitrate tolerance");
  SetFlags(f, Knob::SLIDER | Knob::LOG_SLIDER);

  static const char* mbDecisionTypes[] = {
    "FF_MB_DECISION_SIMPLE", "FF_MB_DECISION_BITS", "FF_MB_DECISION_RD", 0
  };
  Enumeration_knob(f, &mbDecision_, mbDecisionTypes, "mbDecision", "macro block decision mode");

  EndGroup(f);
}

//***************************************************************************//
//        ProresWriter - Execution...                                        //
//***************************************************************************//

void proresWriter::execute()
{
  error_ = IGNORE_FINISH;

  if (!codec_) {
    iop->critical("Codec Prores not found...");
    return;
  }

  AVOutputFormat* fmt = 0;
  fmt = av_guess_format(NULL, filename(), NULL);
  if (!fmt) {
    iop->critical("could not deduce output format from file extension");
    return;
  }

  if (!formatContext_)
    avformat_alloc_output_context2(&formatContext_, fmt, NULL, NULL);
  snprintf(formatContext_->filename, sizeof(formatContext_->filename), "%s", filename());

  PixelFormat pixFMT = AV_PIX_FMT_YUV422P10LE;;

  if (!stream_) {
    stream_ = avformat_new_stream(formatContext_, codec_);
    if (!stream_) {
      iop->critical("Could not allocate stream. Out of memory.");
      return;
    }

    codecContext_ = stream_->codec;

    // this is set to the first element of FMT a choice could be added
    codecContext_->pix_fmt = pixFMT;
    codecContext_->flags |= CODEC_FLAG_GLOBAL_HEADER;

    codecContext_->profile = profilesProres_;
    codecContext_->codec_id = codec_->id;
    codecContext_->codec_type = AVMEDIA_TYPE_VIDEO;
    codecContext_->bit_rate = bitrate_;
    codecContext_->bit_rate_tolerance = bitrateTolerance_;
    codecContext_->width = width();
    codecContext_->height = height();

    av_opt_set(codecContext_->priv_data, "bits_per_mb", "8000", 0);
    av_opt_set(codecContext_->priv_data, "vendor", "ap10", 0);

    const float CONVERSION_FACTOR = 1000.0f;
    codecContext_->time_base.num = (int) CONVERSION_FACTOR;
    codecContext_->time_base.den = (int) (fps_ * CONVERSION_FACTOR);

    codecContext_->gop_size = 1;
    codecContext_->mb_decision = mbDecision_;

    if (avcodec_open2(codecContext_, codec_, NULL) < 0) {
      iop->critical("unable to open codec");
      freeFormat();
      return;
    }

    if (!(formatContext_->oformat->flags & AVFMT_NOFILE)) {
      if (avio_open(&formatContext_->pb, filename(), AVIO_FLAG_WRITE) < 0) {
        iop->critical("unable to open file");
        freeFormat();
        return;
      }
    }

    avformat_write_header(formatContext_, NULL);
  }

  error_ = CLEANUP;

  AVPicture picture;
  int picSize = avpicture_get_size(PIX_FMT_RGB24, width(), height());
  uint8_t* buffer = (uint8_t*) av_malloc(picSize);
  avpicture_fill(&picture, buffer, PIX_FMT_RGB24, width(), height());

  Row row(0, width());
  input0().validate();
  input0().request(0, 0, width(), height(), Mask_RGB, 1);

  for (int y = 0; y < height(); ++y) {
    get(y, 0, width(), Mask_RGB, row);
    if (iop->aborted()) {
      av_free(buffer);
      return;
    }
    for (Channel z = Chan_Red; z <= Chan_Blue; incr(z)) {
      const float* from = row[z];
      to_byte(z - 1, picture.data[0] + (height() - y - 1) * picture.linesize[0] + z - 1, from, NULL, width(), 3);
    }
  }

  AVFrame* output = avcodec_alloc_frame();
  picSize = avpicture_get_size(pixFMT, width(), height());
  uint8_t* outBuffer = (uint8_t*)av_malloc(picSize);

  av_image_alloc(output->data, output->linesize, width(), height(), pixFMT, 1);

  SwsContext* convertCtx = sws_getContext( width(), height(), PIX_FMT_RGB24,
                                           width(), height(), pixFMT,
                                           SWS_BICUBIC, NULL, NULL, NULL );

  int sliceHeight = sws_scale( convertCtx, picture.data, picture.linesize, 0,
                               height(), output->data, output->linesize);
  assert(sliceHeight > 0);

  int ret = 0;
  if ((formatContext_->oformat->flags & AVFMT_RAWPICTURE) != 0) {
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.flags |= AV_PKT_FLAG_KEY;
    pkt.stream_index = stream_->index;
    pkt.data = (uint8_t*) output;
    pkt.size = sizeof(AVPicture);
    ret = av_interleaved_write_frame(formatContext_, &pkt);
  }
  else {
    uint8_t* outbuf = (uint8_t*) av_malloc(picSize);
    assert(outbuf != NULL);
    ret = avcodec_encode_video(codecContext_, outbuf, picSize, output);
    if (ret > 0) {
      AVPacket pkt;
      av_init_packet(&pkt);

      if (codecContext_->coded_frame && static_cast<unsigned long>(codecContext_->coded_frame->pts) != AV_NOPTS_VALUE)
        pkt.pts = av_rescale_q(codecContext_->coded_frame->pts, codecContext_->time_base, stream_->time_base);
      if (codecContext_->coded_frame && codecContext_->coded_frame->key_frame)
        pkt.flags |= AV_PKT_FLAG_KEY;

      pkt.stream_index = stream_->index;
      pkt.data = outbuf;
      pkt.size = ret;

      ret = av_interleaved_write_frame(formatContext_, &pkt);
    }
    else {
      // we've got an error
      char szError[1024];
      av_strerror(ret, szError, 1024);
      iop->error(szError);
    }

    av_free(outbuf);
  }

  av_free(outBuffer);
  av_free(buffer);
  av_free(output);

  if (ret) {
    iop->critical("error writing frame to file");
    return;
  }

  error_ = SUCCESS;
}

void proresWriter::finish()
{
  if (error_ == IGNORE_FINISH)
    return;
  av_write_trailer(formatContext_);
  avcodec_close(codecContext_);
  if (!(formatContext_->oformat->flags & AVFMT_NOFILE))
    avio_close(formatContext_->pb);
  freeFormat();
}

void proresWriter::freeFormat()
{
  for (int i = 0; i < static_cast<int>(formatContext_->nb_streams); ++i)
    av_freep(&formatContext_->streams[i]);
  av_free(formatContext_);
  formatContext_ = NULL;
  stream_ = NULL;
}


static Writer* build(Write* iop) { return new proresWriter(iop); }
const Writer::Description proresWriter::d("prores\0mov\0", build);
