
namespace foleys
{

bool AVMovieClip::openFromFile (const juce::File file)
{
    backgroundJob.setSuspended (true);

    std::unique_ptr<AVReader> reader = AVFormatManager::createReaderFor (file);
    if (reader->isOpenedOk())
    {
        setReader (std::move (reader));
        return true;
    }

    return false;
}

void AVMovieClip::setReader (std::unique_ptr<AVReader> readerToUse)
{
    movieReader = std::move (readerToUse);
    audioFifo.setNumChannels (movieReader->numChannels);
    audioFifo.setSampleRate (movieReader->sampleRate);
    audioFifo.setPosition (0);

    videoFifo.setTimebase (movieReader->timebase);
    videoFifo.setSize (movieReader->originalSize);
    videoFifo.clear();

    backgroundJob.setSuspended (false);

//    thumbnailReader = AVFormatManager::createReaderFor (file, StreamTypes::video());
}

Size AVMovieClip::getOriginalSize() const
{
    return (movieReader != nullptr) ? movieReader->originalSize : Size();
}

double AVMovieClip::getLengthInSeconds() const
{
    if (movieReader && movieReader->isOpenedOk())
        return movieReader->getTotalLength() / sampleRate;

    return {};
}

double AVMovieClip::getCurrentTimeInSeconds() const
{
    return sampleRate == 0 ? 0 : nextReadPosition / sampleRate;
}

Timecode AVMovieClip::getCurrentTimecode() const
{
    return getFrameTimecodeForTime (getCurrentTimeInSeconds());
}

Timecode AVMovieClip::getFrameTimecodeForTime (double time) const
{
    return videoFifo.getFrameTimecodeForTime (time);
}

juce::Image AVMovieClip::getFrame (const Timecode) const
{
    return {};
}

juce::Image AVMovieClip::getStillImage (double seconds, Size size)
{
    if (thumbnailReader)
        return thumbnailReader->getStillImage (seconds, size);

    return {};
}

juce::Image AVMovieClip::getCurrentFrame() const
{
    auto pts = sampleRate > 0 ? nextReadPosition / sampleRate : 0.0;
    if (movieReader && movieReader->sampleRate > 0)
        pts = audioFifo.getReadPosition() / movieReader->sampleRate;

    return videoFifo.getVideoFrame (pts);
}

void AVMovieClip::prepareToPlay (int samplesPerBlockExpected, double sampleRateToUse)
{
    sampleRate = sampleRateToUse;
}

void AVMovieClip::releaseResources()
{
    sampleRate = 0;
}

void AVMovieClip::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    if (movieReader && movieReader->isOpenedOk() && movieReader->hasAudio())
    {
        audioFifo.pullSamples (info);
    }
    else
    {
        info.clearActiveBufferRegion();
    }
    nextReadPosition += info.numSamples;

    triggerAsyncUpdate();
}

bool AVMovieClip::hasVideo() const
{
    return movieReader ? movieReader->hasVideo() : false;
}

bool AVMovieClip::hasAudio() const
{
    return movieReader ? movieReader->hasAudio() : false;
}

bool AVMovieClip::hasSubtitle() const
{
    return movieReader ? movieReader->hasSubtitle() : false;
}

void AVMovieClip::handleAsyncUpdate()
{
    if (sampleRate > 0 && hasVideo())
    {
        auto currentTimecode = videoFifo.getFrameTimecodeForTime (nextReadPosition / sampleRate);
        if (currentTimecode != lastShownFrame)
        {
            sendTimecode (currentTimecode, juce::sendNotificationAsync);
            lastShownFrame = currentTimecode;
        }

        videoFifo.clearFramesOlderThan (lastShownFrame);
    }
}

void AVMovieClip::setNextReadPosition (juce::int64 samples)
{
    backgroundJob.setSuspended (true);

    nextReadPosition = samples;
    audioFifo.setPosition (samples);
    if (movieReader)
        movieReader->setPosition (samples);

    backgroundJob.setSuspended (false);
}

juce::int64 AVMovieClip::getNextReadPosition() const
{
    return nextReadPosition;
}

juce::int64 AVMovieClip::getTotalLength() const
{
    if (movieReader && movieReader->isOpenedOk())
        return movieReader->getTotalLength();

    return 0;
}

bool AVMovieClip::isLooping() const
{
    return loop;
}

void AVMovieClip::setLooping (bool shouldLoop)
{
    loop = shouldLoop;
}

AVMovieClip::BackgroundReaderJob::BackgroundReaderJob (AVMovieClip& ownerToUse)
    : owner (ownerToUse)
{
}

int AVMovieClip::BackgroundReaderJob::useTimeSlice()
{
    if (!suspended && owner.movieReader.get() != nullptr && owner.audioFifo.getFreeSpace() > 2048)
    {
        juce::ScopedValueSetter<bool> guard (inDecodeBlock, true);
        owner.movieReader->readNewData (owner.videoFifo, owner.audioFifo);
        return 0;
    }

    return 50;
}

void AVMovieClip::BackgroundReaderJob::setSuspended (bool s)
{
    suspended = s;

    while (suspended && inDecodeBlock)
        juce::Thread::sleep (5);
}

juce::TimeSliceClient* AVMovieClip::getBackgroundJob()
{
    return &backgroundJob;
}

}
