// Copyright (c) 2014-2014 Josh Blum
// SPDX-License-Identifier: BSL-1.0

#include <Pothos/Framework.hpp>
#include <Pothos/Object/Containers.hpp>
#include <SoapySDR/Device.hpp>
#include <Poco/Format.h>
#include <Poco/Logger.h>
#include <iostream>
#include <future>

class SDRBlock : public Pothos::Block
{
public:
    SDRBlock(const int direction, const Pothos::DType &dtype, const std::vector<size_t> &channels);
    virtual ~SDRBlock(void);

    /*******************************************************************
     * Device object creation
     ******************************************************************/
    void setupDevice(const std::map<std::string, std::string> &deviceArgs);

    /*******************************************************************
     * Delayed method dispatch
     ******************************************************************/
    Pothos::Object opaqueCallHandler(const std::string &name, const Pothos::Object *inputArgs, const size_t numArgs)
    {
        //try to setup the device future first
        if (name == "setupDevice") return Pothos::Block::opaqueCallHandler(name, inputArgs, numArgs);

        //when ready forward the call to the handler
        if (this->isReady()) return Pothos::Block::opaqueCallHandler(name, inputArgs, numArgs);

        //cache attempted settings when not ready
        const bool isSetter = (name.size() > 3 and name.substr(0, 3) == "set");
        if (isSetter) _cachedArgs[name] = std::vector<Pothos::Object>(inputArgs, inputArgs+numArgs);
        else throw Pothos::Exception("SDRBlock::"+name+"()", "device not ready");
        return Pothos::Object();
    }

    /*******************************************************************
     * Stream config
     ******************************************************************/
    void setupStream(const std::map<std::string, std::string> &streamArgs)
    {
        //create format string from the dtype
        std::string format;
        if (_dtype.isComplex()) format += "C";
        if (_dtype.isFloat()) format += "F";
        else if (_dtype.isInteger() and _dtype.isSigned()) format += "S";
        else if (_dtype.isInteger() and not _dtype.isSigned()) format += "U";
        size_t bits = _dtype.elemSize()*8;
        if (_dtype.isComplex()) bits /= 2;
        format += std::to_string(bits);

        //create the stream
        _stream = _device->setupStream(_direction, format, _channels, streamArgs);
        assert(_stream != nullptr);
    }

    void setSampleRate(const double rate)
    {
        for (size_t i = 0; i < _channels.size(); i++)
        {
            _device->setSampleRate(_direction, _channels.at(i), rate);
            _pendingLabels[i]["rxRate"] = Pothos::Object(_device->getSampleRate(_direction, _channels.at(i)));
        }
    }

    double getSampleRate(void) const
    {
        return _device->getSampleRate(_direction, _channels.front());
    }

    std::vector<double> getSampleRates(void) const
    {
        return _device->listSampleRates(_direction, _channels.front());
    }

    void setFrontendMap(const std::string &mapping)
    {
        if (mapping.empty()) return;
        return _device->setFrontendMapping(_direction, mapping);
    }

    std::string getFrontendMap(void) const
    {
        return _device->getFrontendMapping(_direction);
    }

    /*******************************************************************
     * Frequency
     ******************************************************************/
    void setFrequency(const double freq, const std::map<std::string, std::string> &args)
    {
        for (size_t i = 0; i < _channels.size(); i++) this->setFrequency(i, freq, args);
    }

    void setFrequency(const std::vector<double> &freqs, const std::map<std::string, std::string> &args)
    {
        for (size_t i = 0; i < freqs.size(); i++) this->setFrequency(i, freqs[i], args);
    }

    void setFrequency(const size_t chan, const double freq, const std::map<std::string, std::string> &args)
    {
        if (chan >= _channels.size()) return;
        if (freq == 0.0) return;
        _pendingLabels[chan]["rxFreq"] = Pothos::Object(_device->getFrequency(_direction, _channels.at(chan)));
        return _device->setFrequency(_direction, _channels.at(chan), freq, args);
    }

    double getFrequency(const size_t chan) const
    {
        if (chan >= _channels.size()) return 0.0;
        return _device->getFrequency(_direction, _channels.at(chan));
    }

    /*******************************************************************
     * Gain mode
     ******************************************************************/
    void setGainMode(const bool automatic)
    {
        for (size_t i = 0; i < _channels.size(); i++) this->setGainMode(i, automatic);
    }

    void setGainMode(const std::vector<bool> &automatic)
    {
        for (size_t i = 0; i < automatic.size(); i++) this->setGainMode(i, automatic[i]);
    }

    void setGainMode(const size_t chan, const bool automatic)
    {
        if (chan >= _channels.size()) return;
        return _device->setGainMode(_direction, _channels.at(chan), automatic);
    }

    double getGainMode(const size_t chan) const
    {
        if (chan >= _channels.size()) return false;
        return _device->getGainMode(_direction, _channels.at(chan));
    }

    /*******************************************************************
     * Gain
     ******************************************************************/
    void setGain(const double gain)
    {
        for (size_t i = 0; i < _channels.size(); i++) this->setGain(i, gain);
    }

    void setGain(const Pothos::ObjectMap &gain)
    {
        for (size_t i = 0; i < _channels.size(); i++) this->setGain(i, gain);
    }

    void setGain(const Pothos::ObjectVector &gains)
    {
        for (size_t i = 0; i < gains.size(); i++)
        {
            if (gains[i].canConvert(typeid(Pothos::ObjectMap))) this->setGain(i, gains[i].convert<Pothos::ObjectMap>());
            else this->setGain(i, gains[i].convert<double>());
        }
    }

    void setGain(const size_t chan, const std::string &name, const double gain)
    {
        if (chan >= _channels.size()) return;
        return _device->setGain(_direction, _channels.at(chan), name, gain);
    }

    double getGain(const size_t chan, const std::string &name) const
    {
        if (chan >= _channels.size()) return 0.0;
        return _device->getGain(_direction, _channels.at(chan), name);
    }

    void setGain(const size_t chan, const double gain)
    {
        if (chan >= _channels.size()) return;
        return _device->setGain(_direction, _channels.at(chan), gain);
    }

    double getGain(const size_t chan) const
    {
        if (chan >= _channels.size()) return 0.0;
        return _device->getGain(_direction, _channels.at(chan));
    }

    void setGain(const size_t chan, const Pothos::ObjectMap &args)
    {
        if (chan >= _channels.size()) return;
        for (const auto &pair : args)
        {
            const auto name = pair.first.convert<std::string>();
            const auto gain = pair.second.convert<double>();
            _device->setGain(_direction, _channels.at(chan), name, gain);
        }
    }

    std::vector<std::string> getGainNames(const size_t chan) const
    {
        if (chan >= _channels.size()) return std::vector<std::string>();
        return _device->listGains(_direction, _channels.at(chan));
    }

    /*******************************************************************
     * Antennas
     ******************************************************************/
    void setAntenna(const std::string &name)
    {
        for (size_t i = 0; i < _channels.size(); i++) this->setAntenna(i, name);
    }

    void setAntenna(const std::vector<std::string> &names)
    {
        for (size_t i = 0; i < names.size(); i++) this->setAntenna(i, names[i]);
    }

    void setAntenna(const size_t chan, const std::string &name)
    {
        if (chan >= _channels.size()) return;
        if (name.empty()) return;
        return _device->setAntenna(_direction, _channels.at(chan), name);
    }

    std::string getAntenna(const size_t chan) const
    {
        if (chan >= _channels.size()) return "";
        return _device->getAntenna(_direction, _channels.at(chan));
    }

    std::vector<std::string> getAntennas(const size_t chan) const
    {
        if (chan >= _channels.size()) return std::vector<std::string>();
        return _device->listAntennas(_direction, _channels.at(chan));
    }

    /*******************************************************************
     * Bandwidth
     ******************************************************************/
    void setBandwidth(const double bandwidth)
    {
        for (size_t i = 0; i < _channels.size(); i++) this->setBandwidth(i, bandwidth);
    }

    void setBandwidth(const std::vector<double> &bandwidths)
    {
        for (size_t i = 0; i < bandwidths.size(); i++) this->setBandwidth(i, bandwidths[i]);
    }

    void setBandwidth(const size_t chan, const double bandwidth)
    {
        if (chan >= _channels.size()) return;
        if (bandwidth == 0) return;
        return _device->setBandwidth(_direction, _channels.at(chan), bandwidth);
    }

    double getBandwidth(const size_t chan) const
    {
        if (chan >= _channels.size()) return 0.0;
        return _device->getBandwidth(_direction, _channels.at(chan));
    }

    std::vector<double> getBandwidths(const size_t chan) const
    {
        if (chan >= _channels.size()) return std::vector<double>();
        return _device->listBandwidths(_direction, _channels.at(chan));
    }

    /*******************************************************************
     * Clocking config
     ******************************************************************/
    void setClockRate(const double rate)
    {
        if (rate == 0.0) return;
        return _device->setMasterClockRate(rate);
    }

    double getClockRate(void) const
    {
        return _device->getMasterClockRate();
    }

    void setClockSource(const std::string &source)
    {
        if (source.empty()) return;
        return _device->setClockSource(source);
    }

    std::string getClockSource(void) const
    {
        return _device->getClockSource();
    }

    std::vector<std::string> getClockSources(void) const
    {
        return _device->listClockSources();
    }

    void setTimeSource(const std::string &source)
    {
        if (source.empty()) return;
        return _device->setTimeSource(source);
    }

    std::string getTimeSource(void) const
    {
        return _device->getTimeSource();
    }

    std::vector<std::string> getTimeSources(void) const
    {
        return _device->listTimeSources();
    }

    void setHardwareTime(const long long timeNs)
    {
        return _device->setHardwareTime(timeNs);
    }

    long long getHardwareTime(void) const
    {
        return _device->getHardwareTime();
    }

    void setCommandTime(const long long timeNs)
    {
        return _device->setCommandTime(timeNs);
    }

    /*******************************************************************
     * Streaming implementation
     ******************************************************************/
    virtual void activate(void);
    virtual void deactivate(void);
    virtual void work(void) = 0;

protected:
    bool isReady(void);
    void emitActivationSignals(void);

    const int _direction;
    const Pothos::DType _dtype;
    const std::vector<size_t> _channels;
    SoapySDR::Device *_device;
    SoapySDR::Stream *_stream;

    std::map<std::string, std::vector<Pothos::Object>> _cachedArgs;
    std::shared_future<SoapySDR::Device *> _deviceFuture;

    std::vector<std::map<std::string, Pothos::Object>> _pendingLabels;
};