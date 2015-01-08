#include "AvOutputAudio.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <iostream>
#include <stdexcept>

namespace avtranscoder
{

AvOutputAudio::AvOutputAudio( const std::string& audioCodecName )
	: _codec( eCodecTypeEncoder, audioCodecName )
	, _verbose( false )
{
}

void AvOutputAudio::setup()
{
	av_register_all();

	AVCodecContext& avCodecContext( _codec.getAVCodecContext() );

	if( &avCodecContext == NULL )
	{
		throw std::runtime_error( "could not allocate audio codec context" );
	}
	
	// try to open encoder with parameters.
	int ret = avcodec_open2( &avCodecContext, &_codec.getAVCodec(), NULL );
	if( ret < 0 )
	{
		char err[AV_ERROR_MAX_STRING_SIZE];
		av_strerror( ret, err, sizeof(err) );
		std::string msg = "could not open audio encoder " + _codec.getCodecName() +": ";
		msg += err;
		throw std::runtime_error( msg );
	}
}

bool AvOutputAudio::encodeFrame( const Frame& sourceFrame, Frame& codedFrame )
{
#if LIBAVCODEC_VERSION_MAJOR > 54
	AVFrame* frame = av_frame_alloc();
#else
	AVFrame* frame = avcodec_alloc_frame();
#endif

	AVCodecContext& avCodecContext = _codec.getAVCodecContext();

	// Set default frame parameters
#if LIBAVCODEC_VERSION_MAJOR > 54
	av_frame_unref( frame );
#else
	avcodec_get_frame_defaults( frame );
#endif

	const AudioFrame& sourceAudioFrame = static_cast<const AudioFrame&>( sourceFrame );
	
	frame->nb_samples     = sourceAudioFrame.getNbSamples();
	frame->format         = avCodecContext.sample_fmt;
	frame->channel_layout = avCodecContext.channel_layout;
	
	// we calculate the size of the samples buffer in bytes
	int buffer_size = av_samples_get_buffer_size( NULL, avCodecContext.channels, frame->nb_samples, avCodecContext.sample_fmt, 0 );
	if( buffer_size < 0 )
	{
		char err[AV_ERROR_MAX_STRING_SIZE];
		av_strerror( buffer_size, err, sizeof(err) );
		throw std::runtime_error( "EncodeFrame error: buffer size < 0 - " + std::string(err) );
	}

	int retvalue = avcodec_fill_audio_frame( frame, avCodecContext.channels, avCodecContext.sample_fmt, sourceAudioFrame.getPtr(), buffer_size, 0 );
	if( retvalue < 0 )
	{
		char err[AV_ERROR_MAX_STRING_SIZE];
		av_strerror( retvalue, err, sizeof(err) );
		throw std::runtime_error( "EncodeFrame error: avcodec fill audio frame - " + std::string( err ) );
	}
	
	AVPacket packet;
	av_init_packet( &packet );
	
	packet.size = 0;
	packet.data = NULL;
	packet.stream_index = 0;
	
	if( ( avCodecContext.coded_frame ) &&
		( avCodecContext.coded_frame->pts != (int)AV_NOPTS_VALUE ) )
	{
		packet.pts = avCodecContext.coded_frame->pts;
	}

	if( avCodecContext.coded_frame &&
		avCodecContext.coded_frame->key_frame )
	{
		packet.flags |= AV_PKT_FLAG_KEY;
	}
	
#if LIBAVCODEC_VERSION_MAJOR > 53
	int gotPacket = 0;
	int ret = avcodec_encode_audio2( &avCodecContext, &packet, frame, &gotPacket );
	if( ret == 0 && gotPacket == 1 )
	{
		codedFrame.copyData( packet.data, packet.size );
	}
#else
	int ret = avcodec_encode_audio( &avCodecContext, packet.data, packet.size, frame );
	if( ret > 0 )
	{
		codedFrame.copyData( packet.data, packet.size );
	}
#endif
	
	av_free_packet( &packet );
	
#if LIBAVCODEC_VERSION_MAJOR > 54
	av_frame_free( &frame );
	return ret == 0 && gotPacket == 1;
#else
	#if LIBAVCODEC_VERSION_MAJOR > 53
		avcodec_free_frame( &frame );
		return ret == 0 && gotPacket == 1;
	#else
		av_free( frame );
	#endif
#endif
	return ret == 0;
}

bool AvOutputAudio::encodeFrame( Frame& codedFrame )
{
	AVCodecContext& avCodecContext = _codec.getAVCodecContext();

	AVPacket packet;
	av_init_packet( &packet );
	
	packet.size = 0;
	packet.data = NULL;
	packet.stream_index = 0;

#if LIBAVCODEC_VERSION_MAJOR > 53
	int gotPacket = 0;
	int ret = avcodec_encode_audio2( &avCodecContext, &packet, NULL, &gotPacket );
	if( ret == 0 && gotPacket == 1 )
	{
		codedFrame.copyData( packet.data, packet.size );
	}
	av_free_packet( &packet );
	return ret == 0 && gotPacket == 1;

#else
	int ret = avcodec_encode_audio( &avCodecContext, packet.data, packet.size, NULL );
	if( ret > 0 )
	{
		codedFrame.copyData( packet.data, packet.size );
	}
	av_free_packet( &packet );
	return ret == 0;

#endif
}

void AvOutputAudio::setProfile( const ProfileLoader::Profile& profile, const AudioFrameDesc& frameDesc  )
{
	if( ! profile.count( constants::avProfileCodec ) )
	{
		throw std::runtime_error( "The profile " + profile.find( constants::avProfileIdentificatorHuman )->second + " is invalid." );
	}
	
	_codec.setCodec( eCodecTypeEncoder, profile.find( constants::avProfileCodec )->second );
	_codec.setAudioParameters( frameDesc );
	
	for( ProfileLoader::Profile::const_iterator it = profile.begin(); it != profile.end(); ++it )
	{
		if( (*it).first == constants::avProfileIdentificator ||
			(*it).first == constants::avProfileIdentificatorHuman ||
			(*it).first == constants::avProfileType ||
			(*it).first == constants::avProfileCodec )
			continue;

		try
		{
			Option& encodeOption = _codec.getCodecContext().getOption( (*it).first );
			encodeOption.setString( (*it).second );
		}
		catch( std::exception& e )
		{}
	}

	setup();

	for( ProfileLoader::Profile::const_iterator it = profile.begin(); it != profile.end(); ++it )
	{
		if( (*it).first == constants::avProfileIdentificator ||
			(*it).first == constants::avProfileIdentificatorHuman ||
			(*it).first == constants::avProfileType ||
			(*it).first == constants::avProfileCodec )
			continue;

		try
		{
			Option& encodeOption = _codec.getCodecContext().getOption( (*it).first );
			encodeOption.setString( (*it).second );
		}
		catch( std::exception& e )
		{
			if( _verbose )
				std::cout << "[OutputAudio] warning - can't set option " << (*it).first << " to " << (*it).second << ": " << e.what() << std::endl;
		}
	}
}

}

