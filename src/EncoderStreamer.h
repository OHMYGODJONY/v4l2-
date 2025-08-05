#pragma once
/**
 * @file EncoderStreamer.h
 * @author achene
 * @date 2025-08-05
 * @class EncoderStreamer
 * @brief 视频编码推流器，实现从摄像头帧到RTMP流的完整处理流程
 * 
 * 功能包括：初始化FFmpeg编码器、启动/停止编码线程、接收摄像头帧、处理帧数据、
 * 编码为H.264等格式并推流至指定的RTMP服务器。支持自定义图像处理逻辑。
 * 
 * 该类整合了图像处理、FFmpeg编码以及RTMP推流功能，通过多线程实现帧处理与编码推流的异步操作，
 * 支持设置自定义图像处理处理器，适用于实时视频流传输场景。
 */

#include "thread_safe_queue.h"
#include "CameraCapture.h"
#include "ImageProcessor.h"
#include <memory>
#include <atomic>
#include <thread>
#include <string>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
}

class EncoderStreamer {
public:
    /**
     * @brief 构造函数，初始化编码器推流器基本参数
     * @param rtmp_url RTMP服务器地址
     * @param width 视频宽度
     * @param height 视频高度
     * @param fps 视频帧率
     * @param bitrate 视频比特率，默认值为2000000
     */
    EncoderStreamer(const std::string& rtmp_url, 
                   int width, 
                   int height, 
                   int fps,
                   int bitrate = 2000000);

    /**
     * @brief 析构函数，释放资源
     */
    ~EncoderStreamer();

    /**
     * @brief 初始化编码器和推流相关资源
     * @return 初始化成功返回true，否则返回false
     */
    bool initialize();

    /**
     * @brief 启动编码推流线程
     */
    void start();

    /**
     * @brief 停止编码推流线程
     */
    void stop();

    /**
     * @brief 推送摄像头帧到处理队列
     * @param frame 摄像头帧数据
     */
    void push_frame(const CameraFrame& frame);
    
    /**
     * @brief 设置自定义图像处理处理器（智能指针版本）
     * @param processor 图像处理处理器的unique_ptr，所有权会转移
     */
    void set_processor(std::unique_ptr<ImageProcessor> processor){
        processor_ = std::move(processor);
    }
    
    /**
     * @brief 设置自定义图像处理处理器（原始指针版本）
     * @param processor 图像处理处理器的原始指针，内部会转为unique_ptr（所有权转移）
     */
    void set_processor(ImageProcessor* processor) {
        processor_ = std::unique_ptr<ImageProcessor>(processor);
    }
    
private:
    /**
     * @brief 编码循环线程函数，处理队列中的帧并推流
     */
    void encoding_loop();
    
    /**
     * @brief 初始化FFmpeg相关组件
     * @return 初始化成功返回true，否则返回false
     */
    bool init_ffmpeg();
    
    /**
     * @brief 清理FFmpeg相关资源
     */
    void cleanup();
    
    /**
     * @brief 编码并发送帧数据
     * @param frame 待编码的AVFrame
     * @return 编码发送成功返回true，否则返回false
     */
    bool encode_and_send_frame(const AVFrame* frame);
    
private:
    std::unique_ptr<ImageProcessor> processor_ = std::make_unique<ImageProcessor>(); // 默认实例
    std::string rtmp_url_;
    int width_;
    int height_;
    int fps_;
    int bitrate_;
    
    std::atomic<bool> running_{false};
    std::thread encoding_thread_;
    
    // 帧输入队列
    ThreadSafeQueue<CameraFrame> input_queue_;
    
    // FFmpeg 上下文
    AVFormatContext* fmt_ctx_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;
    AVStream* video_stream_ = nullptr;
    SwsContext* rgb_ctx_ = nullptr;
    SwsContext* sws_ctx_ = nullptr;
    AVFrame* rgb_frame_ = nullptr;
    AVFrame* sws_frame_ = nullptr;
    int64_t pts_ = 0;
};