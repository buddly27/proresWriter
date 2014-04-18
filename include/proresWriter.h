#ifndef _NUKE_PRORESWRITER_H_
#define _NUKE_PRORESWRITER_H_

#include "DDImage/Writer.h"
#include "DDImage/Knobs.h"

extern "C" {
#include <errno.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

// Writer class

class proresWriter : public DD::Image::Writer
{
private:
  enum WriterError { SUCCESS = 0, IGNORE_FINISH, CLEANUP };
  void freeFormat();

  AVCodec* codec_;
  AVCodecContext* codecContext_;
  AVFormatContext* formatContext_;
  AVStream* stream_;

  WriterError error_;

  // knobs variables
  int profilesProres_;
  float fps_;
  int bitrate_;
  int bitrateTolerance_;
  int mbDecision_;

public:
  explicit proresWriter( DD::Image::Write* iop );
  virtual ~proresWriter();

  virtual bool movie() const { return true; }

  void execute();
  void finish();
  void knobs( DD::Image::Knob_Callback f );
  static const DD::Image::Writer::Description d;
};

#endif // _NUKE_PRORESWRITER_H_
