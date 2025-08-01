#include <utils/net.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <gui/smgui.h>
#include <gui/widgets/stepped_slider.h>
#include <utils/optionlist.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "network_source",
    /* Description:     */ "UDP/TCP Source Module",
    /* Author:          */ "Ryzerth",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

enum Protocol {
    PROTOCOL_TCP_SERVER,
    PROTOCOL_TCP_CLIENT,
    PROTOCOL_UDP
};

enum SampleType {
    SAMPLE_TYPE_INT8,
    SAMPLE_TYPE_INT16,
    SAMPLE_TYPE_INT32,
    SAMPLE_TYPE_FLOAT32
};

const size_t SAMPLE_TYPE_SIZE[] {
    2*sizeof(int8_t),
    2*sizeof(int16_t),
    2*sizeof(int32_t),
    2*sizeof(float),
};

class NetworkSourceModule : public ModuleManager::Instance {
public:
    NetworkSourceModule(std::string name) {
        this->name = name;

        samplerate = 1000000.0;

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        // Define protocols
        // protocols.define("TCP (Server)", PROTOCOL_TCP_SERVER);
        protocols.define("TCP (Client)", PROTOCOL_TCP_CLIENT);
        protocols.define("UDP", PROTOCOL_UDP);

        // Define sample types
        sampleTypes.define("Int8", SAMPLE_TYPE_INT8);
        sampleTypes.define("Int16", SAMPLE_TYPE_INT16);
        sampleTypes.define("Int32", SAMPLE_TYPE_INT32);
        sampleTypes.define("Float32", SAMPLE_TYPE_FLOAT32);

        // Load config
        config.acquire();
        if (config.conf[name].contains("samplerate")) {
            samplerate = config.conf[name]["samplerate"];
            tempSamplerate = samplerate;
        }
        if (config.conf[name].contains("protocol")) {
            std::string protoStr = config.conf[name]["protocol"];
            if (protocols.keyExists(protoStr)) { proto = protocols.value(protocols.keyId(protoStr)); }
        }
        if (config.conf[name].contains("sampleType")) {
            std::string sampTypeStr = config.conf[name]["sampleType"];
            if (sampleTypes.keyExists(sampTypeStr)) { sampType = sampleTypes.value(sampleTypes.keyId(sampTypeStr)); }
        }
        if (config.conf[name].contains("host")) {
            std::string hostStr = config.conf[name]["host"];
            strcpy(hostname, hostStr.c_str());
        }
        if (config.conf[name].contains("port")) {
            port = config.conf[name]["port"];
            port = std::clamp<int>(port, 1, 65535);
        }
        config.release();

        // Set menu IDs
        protoId = protocols.valueId(proto);
        sampTypeId = sampleTypes.valueId(sampType);

        sigpath::sourceManager.registerSource("网络", &handler);
    }

    ~NetworkSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("网络");
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    std::string getSrScaled(double sr) {
        char buf[1024];
        if (sr >= 1000000.0) {
            sprintf(buf, "%.1lf MS/s", sr / 1000000.0);
        }
        else if (sr >= 1000.0) {
            sprintf(buf, "%.1lf KS/s", sr / 1000.0);
        }
        else {
            sprintf(buf, "%.1lf S/s", sr);
        }
        return std::string(buf);
    }

    static void menuSelected(void* ctx) {
        NetworkSourceModule* _this = (NetworkSourceModule*)ctx;
        core::setInputSampleRate(_this->samplerate);
        flog::info("NetworkSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        NetworkSourceModule* _this = (NetworkSourceModule*)ctx;
        flog::info("NetworkSourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        NetworkSourceModule* _this = (NetworkSourceModule*)ctx;
        if (_this->running) { return; }
        
        // Depends on protocol
        try {
            if (_this->proto == PROTOCOL_TCP_SERVER) {
                // Create TCP listener
                // TODO

                // Start listen worker
                // TODO
            }
            else if (_this->proto == PROTOCOL_TCP_CLIENT) {
                // Connect to TCP server
                _this->sock = net::connect(_this->hostname, _this->port);
            }
            else if (_this->proto == PROTOCOL_UDP) {
                // Open UDP socket
                _this->sock = net::openudp("0.0.0.0", _this->port, _this->hostname, _this->port, true);
            }
        }
        catch (const std::exception& e) {
            flog::error("Could not start Network Source: {}", e.what());
            return;
        }

        // Start receive worker
        _this->workerThread = std::thread(&NetworkSourceModule::worker, _this);

        _this->running = true;
        flog::info("NetworkSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        NetworkSourceModule* _this = (NetworkSourceModule*)ctx;
        if (!_this->running) { return; }

        // Stop listen worker
        // TODO

        // Close connection
        if (_this->sock) { _this->sock->close(); }

        // Stop worker thread
        _this->stream.stopWriter();
        if (_this->workerThread.joinable()) { _this->workerThread.join(); }
        _this->stream.clearWriteStop();

        _this->running = false;
        flog::info("NetworkSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        NetworkSourceModule* _this = (NetworkSourceModule*)ctx;
        if (_this->running) {
            // Nothing for now
        }
        _this->freq = freq;
        flog::info("NetworkSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        NetworkSourceModule* _this = (NetworkSourceModule*)ctx;

        if (_this->running) { SmGui::BeginDisabled(); }

        // Hostname and port field
        if (SmGui::InputText(("##network_source_host_" + _this->name).c_str(), _this->hostname, sizeof(_this->hostname))) {
            config.acquire();
            config.conf[_this->name]["host"] = _this->hostname;
            config.release(true);
        }
        SmGui::SameLine();
        SmGui::FillWidth();
        if (SmGui::InputInt(("##network_source_port_" + _this->name).c_str(), &_this->port, 0, 0)) {
            _this->port = std::clamp<int>(_this->port, 1, 65535);
            config.acquire();
            config.conf[_this->name]["port"] = _this->port;
            config.release(true);
        }

        // Mode protocol selector
        SmGui::LeftLabel("Protocol");
        SmGui::FillWidth();
        if (SmGui::Combo(("##network_source_proto_" + _this->name).c_str(), &_this->protoId, _this->protocols.txt)) {
            _this->proto = _this->protocols.value(_this->protoId);
            config.acquire();
            config.conf[_this->name]["protocol"] = _this->protocols.key(_this->protoId);
            config.release(true);
        }

        // Sample type selector
        SmGui::LeftLabel("Sample type");
        SmGui::FillWidth();
        if (SmGui::Combo(("##network_source_samp_" + _this->name).c_str(), &_this->sampTypeId, _this->sampleTypes.txt)) {
            _this->sampType = _this->sampleTypes.value(_this->sampTypeId);
            config.acquire();
            config.conf[_this->name]["sampleType"] = _this->sampleTypes.key(_this->sampTypeId);
            config.release(true);
        }

        // Samplerate selector
        SmGui::LeftLabel("Samplerate");
        SmGui::FillWidth();
        if (SmGui::InputInt(("##network_source_sr_" + _this->name).c_str(), &_this->tempSamplerate)) {
            // Prevent silly values from silly users
            _this->tempSamplerate = std::max<int>(_this->tempSamplerate, 1000);
        }
        bool applyEn = (!_this->running && _this->tempSamplerate != _this->samplerate);
        if (!applyEn) { SmGui::BeginDisabled(); }
        SmGui::FillWidth();
        if (SmGui::Button(("Apply##network_source_apply_" + _this->name).c_str())) {
            _this->samplerate = _this->tempSamplerate;
            core::setInputSampleRate(_this->samplerate);
            config.acquire();
            config.conf[_this->name]["samplerate"] = _this->samplerate;
            config.release(true);
        }
        if (!applyEn) { SmGui::EndDisabled(); }

        if (_this->tempSamplerate != _this->samplerate) {
            SmGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Warning: Samplerate not applied yet");
        }

        if (_this->running) { SmGui::EndDisabled(); }
    }

    void worker() {
        // Compute sizes
        int blockSize = samplerate / 200;
        int sampleSize = SAMPLE_TYPE_SIZE[sampType];

        // Chose amount of bytes to attempt to read
        bool forceSize = (proto != PROTOCOL_UDP);
        int frameSize = sampleSize * (forceSize ? blockSize : STREAM_BUFFER_SIZE);

        // Allocate receive buffer
        uint8_t* buffer = dsp::buffer::alloc<uint8_t>(frameSize);

        while (true) {
            // Read samples from socket
            int bytes = sock->recv(buffer, frameSize, forceSize);
            if (bytes <= 0) { break; }

            // Convert to CF32 (note: problem if partial sample)
            int count = bytes / sampleSize;
            switch (sampType) {
            case SAMPLE_TYPE_INT8:
                volk_8i_s32f_convert_32f((float*)stream.writeBuf, (int8_t*)buffer, 128.0f, count*2);
                break;
            case SAMPLE_TYPE_INT16:
                volk_16i_s32f_convert_32f((float*)stream.writeBuf, (int16_t*)buffer, 32768.0f, count*2);
                break;
            case SAMPLE_TYPE_INT32:
                volk_32i_s32f_convert_32f((float*)stream.writeBuf, (int32_t*)buffer, 2147483647.0f, count*2);
                break;
            case SAMPLE_TYPE_FLOAT32:
                memcpy(stream.writeBuf, buffer, bytes);
                break;
            default:
                break;
            }

            // Send out converted samples
            if (!stream.swap(count)) { break; }
        }

        // Free receive buffer
        dsp::buffer::free(buffer);
    }

    std::string name;
    bool enabled = true;
    dsp::stream<dsp::complex_t> stream;
    SourceManager::SourceHandler handler;
    bool running = false;
    double freq;
    
    int samplerate = 1000000;
    int tempSamplerate = 1000000;
    Protocol proto = PROTOCOL_UDP;
    int protoId;
    SampleType sampType = SAMPLE_TYPE_INT16;
    int sampTypeId;
    char hostname[1024] = "localhost";
    int port = 1234;

    OptionList<std::string, Protocol> protocols;
    OptionList<std::string, SampleType> sampleTypes;

    std::thread workerThread;
    std::thread listenWorkerThread;

    std::mutex sockMtx;
    std::shared_ptr<net::Socket> sock;
    std::shared_ptr<net::Listener> listener;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(core::args["root"].s() + "/network_source_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new NetworkSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (NetworkSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
