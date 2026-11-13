//
//  VROAudioPlayeriOS.cpp
//  ViroRenderer
//
//  Created by Raj Advani on 11/6/16.
//  Copyright © 2016 Viro Media. All rights reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  "Software"), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "VROAudioPlayeriOS.h"
#include "VROData.h"
#include "VROLog.h"
#include "VROPlatformUtil.h"
#include "VROMath.h"
#include "VROStringUtil.h"

VROAudioPlayeriOS::VROAudioPlayeriOS(std::string url, bool isLocalUrl) :
    _playVolume(1.0),
    _muted(false),
    _paused(false),
    _loop(false),
    _numberOfLoops(0),
    _isLocal(isLocalUrl),
    _url(url) {
}

VROAudioPlayeriOS::VROAudioPlayeriOS(std::shared_ptr<VROData> data) :
    _playVolume(1.0),
    _muted(false),
    _paused(false),
    _loop(false),
    _isLocal(true),
    _numberOfLoops(0) {
    
    _player = [[AVAudioPlayer alloc] initWithData:[NSData dataWithBytes:data->getData() length:data->getDataLength()]
                                            error:NULL];
    [_player prepareToPlay];
}

VROAudioPlayeriOS::VROAudioPlayeriOS(std::shared_ptr<VROSoundData> data) :
    _playVolume(1.0),
    _muted(false),
    _paused(false),
    _loop(false),
    _isLocal(false),
    _numberOfLoops(0) {
    _data = data;
}

void VROAudioPlayeriOS::setDelegate(std::shared_ptr<VROSoundDelegateInternal> delegate) {
    _delegate = delegate;
}

void VROAudioPlayeriOS::setup() {
    if (_data) {
        _data->setDelegate(shared_from_this());
    }
    else if (!_url.empty()) {

        if (_isLocal) {
            NSError *error = nil;
            NSURL *localUrlObj;
            bool hasFilePrefix = VROStringUtil::startsWith(_url, "file:/");
            if (hasFilePrefix) {
                localUrlObj = [NSURL URLWithString:[NSString stringWithUTF8String:_url.c_str()]];
            } else {
                localUrlObj = [NSURL fileURLWithPath:[NSString stringWithUTF8String:_url.c_str()]];
            }

            _player = [[AVAudioPlayer alloc] initWithContentsOfURL:localUrlObj error:&error];
            if (error || !_player){
                _delegate->soundDidFail("Failed to load sound");
                return;
            }
            _audioDelegate = [[VROAudioPlayerDelegate alloc] initWithSoundDelegate:_delegate];
            _player.delegate = _audioDelegate;
            _delegate->soundIsReady();

            [_player prepareToPlay];
        }
        else {
            // Need shared pointer to prevent this object from deleted underneath us
            std::shared_ptr<VROAudioPlayeriOS> shared = shared_from_this();
            NSURL *urlObj = [NSURL URLWithString:[NSString stringWithUTF8String:_url.c_str()]];
            VROPlatformDownloadDataWithURL(urlObj, ^(NSData *data, NSError *error) {
                if (data && !error) {
                    NSError *errorCode = nil;
                    shared->_player = [[AVAudioPlayer alloc] initWithData:data error:&errorCode];
                    shared->updatePlayerProperties();
                    if (errorCode || !shared->_player){
                        shared->_delegate->soundDidFail("Failed to load sound");
                        return;
                    }

                    shared->_audioDelegate = [[VROAudioPlayerDelegate alloc] initWithSoundDelegate:_delegate];
                    shared->_player.delegate = _audioDelegate;
                    shared->_delegate->soundIsReady();
                }
                else {
                    shared->_delegate->soundDidFail("Failed to load sound");
                }
            });
        }
    }
    
    if(_isLocal) {
        _delegate->soundIsReady();
    }
}

VROAudioPlayeriOS::~VROAudioPlayeriOS() {
    if (_player) {
        [_player stop];
    }
}

void VROAudioPlayeriOS::updatePlayerProperties() {
    _player.volume = _muted ? 0 : _playVolume;
}

void VROAudioPlayeriOS::setLoop(bool loop) {
    setLoop(loop, -1);
}

void VROAudioPlayeriOS::setLoop(bool loop, int numberOfLoops) {
    if (_loop == loop) {
        return;
    }

    _loop = loop;
    _numberOfLoops = numberOfLoops;
    if (_player) {
        [_player setNumberOfLoops:_numberOfLoops];
        
        // If we were not explicitly paused and loop was activated,
        // play the sound (so it turns back on)
        if (!_paused && _loop) {
            [_player play];
        }
    }
}

void VROAudioPlayeriOS::setMuted(bool muted) {
    _muted = muted;
    if (_player) {
        _player.volume = muted ? 0 : _playVolume;
    }
}

void VROAudioPlayeriOS::seekToTime(float seconds) {
    if (_player) {
        seconds = clamp(seconds, 0, _player.duration);
        _player.currentTime = seconds;
    }
}

void VROAudioPlayeriOS::setVolume(float volume) {
    _playVolume = volume;
    if (_player) {
        _player.volume = volume;
    }
}

void VROAudioPlayeriOS::play() {
    _paused = false;
    if (_player) {
        _player.volume = _muted ? 0 : _playVolume;
        [_player setNumberOfLoops:_numberOfLoops];
        [_player play];
    }
}

void VROAudioPlayeriOS::play(double atTime) {
    _paused = false;
    if (_player) {
        _player.volume = _muted ? 0 : _playVolume;
        [_player setNumberOfLoops:_numberOfLoops];
        [_player playAtTime:atTime];
    }
}

double VROAudioPlayeriOS::getAudioDuration() {
    if (!_player) {
        return -1;
    }
    
    return [_player duration];
}

double VROAudioPlayeriOS::getDeviceCurrentTime() {
    if (!_player) {
        return -1;
    }
    
    return [_player deviceCurrentTime];
}

void VROAudioPlayeriOS::pause() {
    _paused = true;
    if (_player) {
        if([_player isPlaying]) {
            [_player pause];
        }
    }
}

#pragma mark VROSoundDataDelegate Implementation

void VROAudioPlayeriOS::dataIsReady() {
    if (!_player) {
        // A downloaded sound comes back as a bare path in the temp directory, which
        // URLWithString turns into a URL with no scheme that AVAudioPlayer cannot open.
        // A local source keeps its file:// URI.
        std::string localPath = _data->getLocalFilePath();
        NSString *pathString = [NSString stringWithUTF8String:localPath.c_str()];
        NSURL *fileURL = VROStringUtil::startsWith(localPath, "/") ? [NSURL fileURLWithPath:pathString]
                                                                   : [NSURL URLWithString:pathString];
        NSError *error = nil;
        _player = [[AVAudioPlayer alloc] initWithContentsOfURL:fileURL error:&error];
        if (!_player) {
            if (_delegate) {
                _delegate->soundDidFail(error ? std::string([[error localizedDescription] UTF8String])
                                              : "Failed to open sound file");
            }
            return;
        }
        // As on the URL path: the AVAudioPlayer delegate reports the end of the
        // sound (onFinish), and soundIsReady lets the bridge apply its props.
        _audioDelegate = [[VROAudioPlayerDelegate alloc] initWithSoundDelegate:_delegate];
        _player.delegate = _audioDelegate;
        [_player prepareToPlay];
        updatePlayerProperties();

        if (!_paused) {
            [_player play];
        }
        if (_delegate) {
            _delegate->soundIsReady();
        }
    }
}

void VROAudioPlayeriOS::dataError(std::string error) {
    if (_delegate) {
        _delegate->soundDidFail(error);
    }
}

#pragma mark - VROAudioPlayerDelegate implementation

@implementation VROAudioPlayerDelegate {
    std::weak_ptr<VROSoundDelegateInternal> _delegate;
}

- (id)initWithSoundDelegate:(std::shared_ptr<VROSoundDelegateInternal>)soundDelegate {
    self = [super init];
    if (self) {
        _delegate = soundDelegate;
    }
    return self;
}

- (void)audioPlayerDidFinishPlaying:(AVAudioPlayer *)player successfully:(BOOL)flag {
    std::shared_ptr<VROSoundDelegateInternal> soundDelegate = _delegate.lock();
    if (soundDelegate) {
        soundDelegate->soundDidFinish();
    }
}

@end
