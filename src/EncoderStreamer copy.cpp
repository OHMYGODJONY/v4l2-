#include "EncoderStreamer.h"
#include <iostream>
#include <stdexcept>
#define ENCODEC_NAME "h264_rkmpp"

EncoderStreamer::EncoderStreamer(const std::string& rtmp_url, 
                               int width, 
                               int height, 
                               int fps,
                               int bitrate)
    : rtmp_url_(rtmp_url),
      width_(width),
      height_(height),
      fps_(fps),
      bitrate_(bitrate) {}

EncoderStreamer::~EncoderStreamer() {
    stop();
    cleanup();
}

bool EncoderStreamer::initialize() {
    return init_ffmpeg();
}

void EncoderStreamer::start() {
    if (running_) return;
    
    running_ = true;
    encoding_thread_ = std::thread(&EncoderStreamer::encoding_loop, this);
}

void EncoderStreamer::stop() {
    running_ = false;
    if (encoding_thread_.joinable()) {
        encoding_thread_.join();
    }
}

void EncoderStreamer::push_frame(const CameraFrame& frame) {
    input_queue_.push(frame);
}

void EncoderStreamer::encoding_loop() {
    while (running_) {
        CameraFrame frame;
        if (input_queue_.pop(frame, 50)) { // 50ms超时  
            // 使用SWS转换YUYV到YUV420P
            uint8_t* src_data[1] = {static_cast<uint8_t*>(frame.data)};
            int src_linesize[1] = {static_cast<int>(frame.stride)};
            
            sws_scale(sws_ctx_, 
                     src_data, src_linesize, 
                     0, height_,
                     sws_frame_->data, sws_frame_->linesize);
            
            // 设置时间戳
            sws_frame_->pts = pts_++;
            sws_frame_->best_effort_timestamp = sws_frame_->pts;
            
            // 编码并发送
            if (!encode_and_send_frame(sws_frame_)) {
                std::cerr << "Encoding failed for frame: " << frame.sequence << std::endl;
            }
            
            // 归还摄像头缓冲区
            if (frame.return_buffer) {
                frame.return_buffer();
            }
        }
    }
    
    // 刷新编码器
    encode_and_send_frame(nullptr);
}

bool EncoderStreamer::init_ffmpeg() {
    avformat_network_init();
    std::cout << "avformat_network_init success !!!" << std::endl;
    // 初始化输出格式上下文
    avformat_alloc_output_context2(&fmt_ctx_, nullptr, "flv", rtmp_url_.c_str());
    if (!fmt_ctx_) {
        std::cerr << "Could not create output context" << std::endl;
        return false;
    }
    
    // 查找编码器
    const AVCodec* codec = avcodec_find_encoder_by_name(ENCODEC_NAME);
    if (!codec) {
        std::cerr << "hardware Codec not found,choose software Codec" << std::endl;
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if(!codec){
            std::cerr << "software Codec not found" << std::endl;
            return false;
        }
    }
    
    // 创建编码器上下文
    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        std::cerr << "Could not allocate video codec context" << std::endl;
        return false;
    }
    
    // 配置编码器
    codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    codec_ctx_->codec_id = codec->id;
    codec_ctx_->thread_count = 8;
    codec_ctx_->bit_rate = bitrate_;
    codec_ctx_->width = width_;
    codec_ctx_->height = height_;
    codec_ctx_->time_base = (AVRational){1, fps_};
    codec_ctx_->framerate = (AVRational){fps_, 1};
    codec_ctx_->gop_size = 10;
    codec_ctx_->max_b_frames = 0;
    codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    
    // H264预设
    AVDictionary *codec_options = NULL;
    av_dict_set(&codec_options, "crf", "23", 0);
    av_dict_set(&codec_options, "preset", "ultrafast", 0);
    
    // 打开编码器
    if (avcodec_open2(codec_ctx_, codec, &codec_options) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        return false;
    }
    std::cout << "avcodec_open2 success!" << std::endl;
    
    // 创建输出流
    video_stream_ = avformat_new_stream(fmt_ctx_, codec);
    if (!video_stream_) {
        std::cerr << "Failed allocating output stream" << std::endl;
        return false;
    }
    video_stream_->codecpar->codec_tag = 0;
    video_stream_->time_base = codec_ctx_->time_base;
    
    // 复制编码参数到流
    if (avcodec_parameters_from_context(video_stream_->codecpar, codec_ctx_) < 0) {
        std::cerr << "Failed to copy codec parameters" << std::endl;
        return false;
    }

    fmt_ctx_->max_delay = 0;  // 消除格式容器延迟
    av_dict_set(&fmt_ctx_->metadata, "stimeout", "2000000", 0); // 2秒超时
    av_dict_set_int(&fmt_ctx_->metadata, "buffer_size", 1024*400, 0);
    av_dict_set_int(&fmt_ctx_->metadata, "fifo_size", 1024*100, 0);
    
    // 打开输出
    if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&fmt_ctx_->pb, rtmp_url_.c_str(), AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Could not open output URL: " << rtmp_url_ << std::endl;
            return false;
        }
    }
    
    // 写入文件头
    if (avformat_write_header(fmt_ctx_, nullptr) < 0) {
        std::cerr << "Error occurred when opening output URL" << std::endl;
        return false;
    }
    
    // 初始化图像转换器
    sws_ctx_ = sws_getCachedContext(sws_ctx_, 
                             width_, height_, AV_PIX_FMT_YUYV422,
                             width_, height_, AV_PIX_FMT_YUV420P,
                             SWS_BILINEAR, 0, 0, 0);
    if (!sws_ctx_) {
        std::cerr << "Could not initialize the conversion context" << std::endl;
        return false;
    }
    
    // 创建用于转换的帧
    sws_frame_ = av_frame_alloc();
    sws_frame_->format = AV_PIX_FMT_YUV420P;
    sws_frame_->width = width_;
    sws_frame_->height = height_;
    sws_frame_->pts = 0;
    if (av_frame_get_buffer(sws_frame_, 32) < 0) {
        std::cerr << "Could not allocate the video frame data" << std::endl;
        return false;
    }
    std::cout << "init ffmpeg end" << std::endl;
    
    return true;
}

bool EncoderStreamer::encode_and_send_frame(const AVFrame* frame) {
    // 发送帧到编码器
    int ret = avcodec_send_frame(codec_ctx_, frame);
    if (ret < 0) {
        std::cerr << "Error sending a frame to the encoder: " << ret << std::endl;
        return false;
    }
    
    AVPacket* pkt = av_packet_alloc();
    if (!pkt){
        return -1;
    }
    
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx_, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            std::cerr << "Error during encoding: " << ret << std::endl;
            av_packet_unref(pkt);
            return false;
        }
        
        // 重新缩放PTS/DTS
        av_packet_rescale_ts(pkt, codec_ctx_->time_base, video_stream_->time_base);
        pkt->stream_index = video_stream_->index;
        
        // 写入帧
        ret = av_interleaved_write_frame(fmt_ctx_, pkt);
        if (ret < 0) {
            std::cerr << "Error while writing video packet: " << ret << std::endl;
        }
        
        av_packet_unref(pkt);
    }
    
    return true;
}

void EncoderStreamer::cleanup() {
    if (fmt_ctx_ && !(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&fmt_ctx_->pb);
    }
    
    if (fmt_ctx_) {
        avformat_free_context(fmt_ctx_);
        fmt_ctx_ = nullptr;
    }
    
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
    }
    
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    
    if (sws_frame_) {
        av_frame_free(&sws_frame_);
        sws_frame_ = nullptr;
    }
}