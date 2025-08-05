#include "CameraCapture.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <system_error>

#define MODULE_TEST 0

// V4L2操作宏
#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define IOCTL_RETRY(fd, request, arg) \
    ({ \
        int _ret; \
        do { \
            _ret = ioctl(fd, request, arg); \
        } while (_ret == -1 && (errno == EINTR || errno == EAGAIN)); \
        _ret; \
    })

CameraCapture::CameraCapture(const std::string& device_path, 
                           uint32_t width, 
                           uint32_t height, 
                           uint32_t fps,
                           uint32_t pixel_format)
    : device_path_(device_path),
      width_(width),
      height_(height),
      fps_(fps),
      pixel_format_(pixel_format) {}

CameraCapture::~CameraCapture() {
    stop();
    if (initialized_) {
        uninit_device();
    }
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
    }
}

bool CameraCapture::initialize() {
    if (initialized_) return true;
    
    // 打开设备
    fd_ = open(device_path_.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (fd_ == -1) {
        report_error("Failed to open device: " + device_path_);
        return false;
    }
    
    // 检查设备能力
    v4l2_capability cap;
    if (IOCTL_RETRY(fd_, VIDIOC_QUERYCAP, &cap) == -1) {
        report_error("VIDIOC_QUERYCAP failed");
        return false;
    }
    //检查设备是否支持视频捕获功能
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        report_error("Device does not support video capture");
        return false;
    }
    //检查设备是否支持流式 I/O
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        report_error("Device does not support streaming I/O");
        return false;
    }
    
    // 设置格式
    v4l2_format fmt;
    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;//设置缓冲区类型为视频捕获
    fmt.fmt.pix.width = width_;
    fmt.fmt.pix.height = height_;
    fmt.fmt.pix.pixelformat = pixel_format_;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;//设置场模式为任意模式
    
    if (IOCTL_RETRY(fd_, VIDIOC_S_FMT, &fmt) == -1) {
        report_error("Failed to set video format");
        return false;
    }
    // 检查实际设置
    if (fmt.fmt.pix.width != width_ || fmt.fmt.pix.height != height_ || 
        fmt.fmt.pix.pixelformat != pixel_format_) {
            report_error("Device does not support requested format");
            return false;
        }
    stride_ = fmt.fmt.pix.bytesperline;
    
    // 设置帧率
    v4l2_streamparm parm;
    CLEAR(parm);
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = fps_;
    
    if (IOCTL_RETRY(fd_, VIDIOC_S_PARM, &parm) == -1) {
        report_error("Failed to set frame rate");
        return false;
    }
    
    // 初始化缓冲区
    if (!request_buffers()) {
        return false;
    }
    
    initialized_ = true;
    return true;
}

void CameraCapture::start() {
    if (!initialized_) {
        if (!initialize()) {
            return;
        }
    }
    
    if (running_) return;
    
    // 开始流
    if (!start_streaming()) {
        report_error("Failed to start streaming");
        return;
    }
    
    running_ = true;
    capture_thread_ = std::make_unique<std::thread>(&CameraCapture::capture_thread, this);
}

void CameraCapture::stop() {
    if (!running_) return;
    
    running_ = false;
    if (capture_thread_ && capture_thread_->joinable()) {
        capture_thread_->join();
    }
    capture_thread_.reset();
    
    stop_streaming();
}

void CameraCapture::set_frame_callback(const FrameCallback &callback) {
    frame_callback_ = callback;
}

void CameraCapture::set_frame_callback(FrameCallback &&callback) {
    frame_callback_ = std::move(callback);
}

void CameraCapture::capture_thread() {
    while (running_) {
        CameraFrame frame;
        if (get_frame(frame)) {
            if (frame_callback_) {
                frame_callback_(frame);
            }
            //return_buffer_to_queue(frame.buf_index);//异步操作，直接释放缓冲区可能导致数据失效，由调用者释放
        } else {
            // 短暂休眠后重试
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

bool CameraCapture::init_device() {
    // 请求缓冲区
    if (!request_buffers()) {
        return false;
    }
    return true;
}

void CameraCapture::uninit_device() {
    // 解除内存映射
    for (auto& buffer : buffers_) {
        if (buffer.start) {
            munmap(buffer.start, buffer.length);
            buffer.start = nullptr;
            buffer.length = 0;
        }
        if (buffer.dma_fd != -1) {
            close(buffer.dma_fd);
            buffer.dma_fd = -1;
        }
    }
    buffers_.clear();
}

bool CameraCapture::request_buffers() {
    // 请求缓冲区
    v4l2_requestbuffers req;
    CLEAR(req);
    req.count = 4;  // 请求4个缓冲区
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    //请求缓冲区
    if (IOCTL_RETRY(fd_, VIDIOC_REQBUFS, &req) == -1) {
        report_error("VIDIOC_REQBUFS failed");
        return false;
    }
    
    if (req.count < 2) {
        report_error("Insufficient buffer memory");
        return false;
    }
    
    buffers_.resize(req.count);
    
    // 映射缓冲区
    for (size_t i = 0; i < buffers_.size(); ++i) {
        v4l2_buffer buf;
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (IOCTL_RETRY(fd_, VIDIOC_QUERYBUF, &buf) == -1) {
            report_error("VIDIOC_QUERYBUF failed");
            return false;
        }
        
        buffers_[i].length = buf.length;
        //将缓冲区映射到内存中
        buffers_[i].start = mmap(nullptr, buf.length, 
                                PROT_READ | PROT_WRITE, 
                                MAP_SHARED, 
                                fd_, buf.m.offset);
        
        if (buffers_[i].start == MAP_FAILED) {
            report_error("mmap failed");
            return false;
        }
        
        // 导出DMA-BUF
        buffers_[i].dma_fd = export_dma_buf(i);
        if (buffers_[i].dma_fd == -1) {
            report_error("Failed to export DMA-BUF");
        }
    }
    
    // 将缓冲区加入队列
    for (size_t i = 0; i < buffers_.size(); ++i) {
        v4l2_buffer buf;
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (IOCTL_RETRY(fd_, VIDIOC_QBUF, &buf) == -1) {
            report_error("VIDIOC_QBUF failed");
            return false;
        }
    }
    
    return true;
}

bool CameraCapture::start_streaming() {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (IOCTL_RETRY(fd_, VIDIOC_STREAMON, &type) == -1) {
        report_error("VIDIOC_STREAMON failed");
        return false;
    }
    return true;
}

bool CameraCapture::stop_streaming() {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (IOCTL_RETRY(fd_, VIDIOC_STREAMOFF, &type) == -1) {
        report_error("VIDIOC_STREAMOFF failed");
        return false;
    }
    return true;
}

bool CameraCapture::get_frame(CameraFrame& frame) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);
    
    struct timeval tv;
    tv.tv_sec = 2;  // 2秒超时
    tv.tv_usec = 0;
    
    int r = select(fd_ + 1, &fds, nullptr, nullptr, &tv);
    if (r == -1) {
        if (errno == EINTR) return false;  // 中断，重试
        report_error("select failed");
        return false;
    }
    
    if (r == 0) {
        report_error("Capture timeout");
        return false;
    }
    
    v4l2_buffer buf;
    CLEAR(buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    if (IOCTL_RETRY(fd_, VIDIOC_DQBUF, &buf) == -1) {
        if (errno == EAGAIN) return false;  // 非阻塞模式，无数据
        report_error("VIDIOC_DQBUF failed");
        return false;
    }
    
    if (buf.index >= buffers_.size()) {
        report_error("Invalid buffer index");
        return false;
    }
    
    // 填充帧数据
    std::cout << "Captured frame - buf.index: " << buf.index
              << ", data: " << buffers_[buf.index].start
              << ", length: " << buffers_[buf.index].length
              << ", width: " << width_
              << ", height: " << height_
              << ", stride: " << stride_
              << ", fd: " << buffers_[buf.index].dma_fd << std::endl;
    frame.camera_id = camera_id_;
    frame.buf_index = buf.index;
    frame.fd = buffers_[buf.index].dma_fd;
    frame.data = buffers_[buf.index].start;
    frame.length = buffers_[buf.index].length;
    frame.bytes_used = buf.bytesused;
    frame.width = width_;
    frame.height = height_;
    frame.stride = stride_; // 设置步长
    frame.pixel_format = pixel_format_;
    frame.timestamp = buf.timestamp;
    frame.sequence = buf.sequence;
    frame.return_buffer = [this, index = buf.index]() {
        this->return_buffer_to_queue(index);
    };
    
    return true;
}

bool CameraCapture::return_buffer_to_queue(int index) {
    if (index < 0 || static_cast<size_t>(index) >= buffers_.size()) {
        return false;
    }
    
    v4l2_buffer buf;
    CLEAR(buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    
    if (IOCTL_RETRY(fd_, VIDIOC_QBUF, &buf) == -1) {
        report_error("VIDIOC_QBUF failed");
        return false;
    }
    
    return true;
}

int CameraCapture::export_dma_buf(int index) {
    v4l2_exportbuffer expbuf;
    CLEAR(expbuf);
    expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    expbuf.index = index;
    expbuf.flags = O_CLOEXEC | O_RDWR;
    
    if (IOCTL_RETRY(fd_, VIDIOC_EXPBUF, &expbuf) == -1) {
        return -1;
    }
    
    return expbuf.fd;
}

void CameraCapture::report_error(const std::string& message) {
    std::cerr << "CameraCapture[" << device_path_ << "]: " << message;
    if (errno) {
        std::cerr << " (errno: " << errno << " - " << strerror(errno) << ")";
    }
    std::cerr << std::endl;
}


#if  MODULE_TEST
//g++ -o test_video_capture CameraCapture.cpp -lpthread
#include <iostream>
int main() {
    // 创建摄像头实例
    CameraCapture cam1("/dev/video0", 640, 480, 30);
    cam1.set_camera_id(0);
    cam1.set_frame_callback([](const CameraFrame& frame) {
            // 将原始帧放入预处理队列
            std::cout << "frame from video0 read" << std::endl;
            if(frame.return_buffer){
                frame.return_buffer();
            }
        });
    
    CameraCapture cam2("/dev/video2", 640, 480, 30);
    cam2.set_camera_id(1);
    cam2.set_frame_callback([](const CameraFrame& frame) {
            // 将原始帧放入预处理队列
            std::cout << "frame from video2 read" << std::endl;
            if(frame.return_buffer){
                frame.return_buffer();
            }
        });
    
    // 初始化并启动
    if (cam1.initialize() && cam2.initialize()) {
        cam1.start();
        cam2.start();
        
        // 主线程运行一段时间
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        // 停止采集
        cam1.stop();
        cam2.stop();
    }
    
    return 0;
}
#endif