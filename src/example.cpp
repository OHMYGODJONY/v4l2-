
#include "CameraCapture.h"
#include "EncoderStreamer.h"
#include "ImageProcessor.h"
#include <vector>
#include <memory>
#include <iostream>
#include <csignal>

class GrayImageProcessor : public ImageProcessor
{
public:
    void processFrame(cv::Mat& mat) override{
        cv::cvtColor(mat, mat, cv::COLOR_BGR2GRAY);
    }
};


std::atomic<bool> running(true);

void signal_handler(int signum) {
    running = false;
}

int main() {
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 摄像头配置
    struct CameraConfig {
        std::string device;
        std::string rtmp_url;
        int width;
        int height;
        int fps;
    };
    //v4l2-ctl -d /dev/video0 --list-formats-ext 查看摄像头支持格式
    std::vector<CameraConfig> camera_configs = {
        {"/dev/video0", "rtmp://192.168.3.6/live/stream1", 640, 480, 30},
        {"/dev/video2", "rtmp://192.168.3.6/live/stream2", 640, 480, 30}
    };
    
    CameraCapture cam1(camera_configs[0].device, camera_configs[0].width, camera_configs[0].height, camera_configs[0].fps);
    CameraCapture cam2(camera_configs[1].device, camera_configs[1].width, camera_configs[1].height, camera_configs[1].fps);

    EncoderStreamer stream1(camera_configs[0].rtmp_url, camera_configs[0].width, camera_configs[0].height, camera_configs[0].fps);
    EncoderStreamer stream2(camera_configs[1].rtmp_url, camera_configs[1].width, camera_configs[1].height, camera_configs[1].fps);
    //方法1，
    // auto gray_processor = std::make_unique<GrayImageProcessor>();
    // stream1.set_processor(std::move(gray_processor));
    //方法2，
    ImageProcessor* gray_processor = new GrayImageProcessor();
    stream1.set_processor(gray_processor);

    cam1.set_frame_callback([&stream1](const CameraFrame& frame) {
            stream1.push_frame(frame);
    });
    cam2.set_frame_callback([&stream2](const CameraFrame& frame) {
        stream2.push_frame(frame);
    });

    if (cam1.initialize()) {
        cam1.set_camera_id(0);
        cam1.start();
    } else {
        std::cerr << "Failed to initialize camera " << 0 << std::endl;
    }

    if (cam2.initialize()) {
        cam2.set_camera_id(1);
        cam2.start();
    } else {
        std::cerr << "Failed to initialize camera " << 1 << std::endl;
    }

    if (!stream1.initialize()) {
        std::cerr << "Failed to initialize streamer for camera " << 0 << std::endl;
    }
    if (!stream2.initialize()) {
        std::cerr << "Failed to initialize streamer for camera " << 1 << std::endl;
    }

    stream1.start();
    stream2.start();

    while (running) {
        // 监控状态或处理其他任务
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 输出状态信息
        std::cout << "Running... (" << 2 << " streams active)" << std::endl;
    }
    
    cam1.stop();
    cam2.stop();

    stream1.stop();
    stream2.stop();
    
    std::cout << "All streams stopped. Exiting." << std::endl;
    return 0;
}