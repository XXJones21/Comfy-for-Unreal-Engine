// Copyright Comfy Org. All Rights Reserved.

#include "ComfyRuntimeLibrary.h"
#include "ComfyCoreModule.h"
#include "ComfyJob.h"
#include "Audio.h"
#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Sound/SoundWaveProcedural.h"

UTexture2D* UComfyRuntimeLibrary::LoadTextureFromFile(const FString& FilePath, bool bSRGB)
{
	FImage Image;
	if (!FImageUtils::LoadImage(*FilePath, Image))
	{
		UE_LOG(LogComfy, Warning, TEXT("Couldn't load image %s"), *FilePath);
		return nullptr;
	}
	Image.ChangeFormat(ERawImageFormat::BGRA8, bSRGB ? EGammaSpace::sRGB : EGammaSpace::Linear);

	UTexture2D* Texture = FImageUtils::CreateTexture2DFromImage(Image);
	if (Texture)
	{
		Texture->SRGB = bSRGB;
		Texture->UpdateResource();
	}
	return Texture;
}

const FComfyOutputFile* UComfyRuntimeLibrary::FindOutput(const UComfyJob* Job, const FString& OutputName, EComfyOutputKind Kind)
{
	if (!Job)
	{
		return nullptr;
	}
	for (const FComfyOutputFile& File : Job->Outputs)
	{
		if (OutputName.IsEmpty() ? File.Kind == Kind : (File.Name == OutputName || File.NodeId == OutputName))
		{
			return &File;
		}
	}
	return nullptr;
}

UTexture2D* UComfyRuntimeLibrary::LoadTextureFromJob(UComfyJob* Job, const FString& OutputName, bool bSRGB)
{
	const FComfyOutputFile* File = FindOutput(Job, OutputName, EComfyOutputKind::Image);
	return File ? LoadTextureFromFile(File->LocalPath, bSRGB) : nullptr;
}

USoundWaveProcedural* UComfyRuntimeLibrary::LoadSoundFromFile(const FString& FilePath)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
	{
		UE_LOG(LogComfy, Warning, TEXT("Couldn't read %s"), *FilePath);
		return nullptr;
	}

	FWaveModInfo Wave;
	FString Error;
	if (!Wave.ReadWaveInfo(Bytes.GetData(), Bytes.Num(), &Error))
	{
		UE_LOG(LogComfy, Warning, TEXT("%s isn't a WAV file Unreal can read: %s"), *FilePath, *Error);
		return nullptr;
	}
	if (*Wave.pFormatTag != 1 || *Wave.pBitsPerSample != 16)
	{
		UE_LOG(LogComfy, Warning, TEXT("%s must be 16-bit PCM to load at runtime (format %d, %d bits). Import it in the editor instead."),
			*FilePath, *Wave.pFormatTag, *Wave.pBitsPerSample);
		return nullptr;
	}

	const int32 SampleRate = *Wave.pSamplesPerSec;
	const int32 Channels = *Wave.pChannels;

	USoundWaveProcedural* Sound = NewObject<USoundWaveProcedural>();
	Sound->SetSampleRate(SampleRate);
	Sound->NumChannels = Channels;
	Sound->Duration = float(Wave.SampleDataSize) / float(SampleRate * Channels * sizeof(int16));
	Sound->SoundGroup = SOUNDGROUP_Default;
	Sound->bLooping = false;
	Sound->QueueAudio(Wave.SampleDataStart, Wave.SampleDataSize);
	return Sound;
}

USoundWaveProcedural* UComfyRuntimeLibrary::LoadSoundFromJob(UComfyJob* Job, const FString& OutputName)
{
	const FComfyOutputFile* File = FindOutput(Job, OutputName, EComfyOutputKind::Audio);
	return File ? LoadSoundFromFile(File->LocalPath) : nullptr;
}

namespace ComfyImage
{
	static bool Load(const FString& Path, FImage& Out)
	{
		if (!FImageUtils::LoadImage(*Path, Out))
		{
			UE_LOG(LogComfy, Warning, TEXT("Couldn't load image %s"), *Path);
			return false;
		}
		Out.ChangeFormat(ERawImageFormat::RGBA32F, EGammaSpace::Linear);
		return true;
	}

	static bool Save(const FString& Path, FImage& Image, EGammaSpace Gamma)
	{
		Image.ChangeFormat(ERawImageFormat::BGRA8, Gamma);
		if (!FImageUtils::SaveImageByExtension(*Path, Image))
		{
			UE_LOG(LogComfy, Warning, TEXT("Couldn't save image %s"), *Path);
			return false;
		}
		return true;
	}

	static float SmoothStep01(float X)
	{
		X = FMath::Clamp(X, 0.f, 1.f);
		return X * X * (3.f - 2.f * X);
	}
}

bool UComfyRuntimeLibrary::MakeTileable(const FString& InputPath, const FString& OutputPath, float BlendWidth)
{
	FImage Source;
	if (!ComfyImage::Load(InputPath, Source))
	{
		return false;
	}

	const int32 W = Source.SizeX;
	const int32 H = Source.SizeY;
	const float Blend = FMath::Clamp(BlendWidth, 0.05f, 0.5f);
	const float BandX = FMath::Max(1.f, Blend * W * 0.5f);
	const float BandY = FMath::Max(1.f, Blend * H * 0.5f);

	TArrayView64<FLinearColor> In = Source.AsRGBA32F();
	FImage Result(W, H, ERawImageFormat::RGBA32F, EGammaSpace::Linear);
	TArrayView64<FLinearColor> Out = Result.AsRGBA32F();

	// Offsetting by half moves the original edges to the middle and leaves the borders seamless.
	// Near the middle cross, fade back to the unshifted image, which is continuous there.
	for (int32 Y = 0; Y < H; ++Y)
	{
		const float WeightY = 1.f - ComfyImage::SmoothStep01(FMath::Abs(Y + 0.5f - H * 0.5f) / BandY);
		const int32 ShiftedY = (Y + H / 2) % H;
		for (int32 X = 0; X < W; ++X)
		{
			const float WeightX = 1.f - ComfyImage::SmoothStep01(FMath::Abs(X + 0.5f - W * 0.5f) / BandX);
			const float Weight = FMath::Max(WeightX, WeightY);
			const int32 ShiftedX = (X + W / 2) % W;

			const FLinearColor& Shifted = In[int64(ShiftedY) * W + ShiftedX];
			const FLinearColor& Original = In[int64(Y) * W + X];
			Out[int64(Y) * W + X] = FMath::Lerp(Shifted, Original, Weight);
		}
	}

	// The result already tiles. Shift it back so the subject sits where it was in the input;
	// the blended cross moves to the borders, where it wraps cleanly.
	FImage Final(W, H, ERawImageFormat::RGBA32F, EGammaSpace::Linear);
	TArrayView64<FLinearColor> FinalPixels = Final.AsRGBA32F();
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			FinalPixels[int64((Y + H / 2) % H) * W + (X + W / 2) % W] = Out[int64(Y) * W + X];
		}
	}

	return ComfyImage::Save(OutputPath, Final, EGammaSpace::sRGB);
}

bool UComfyRuntimeLibrary::MakeNormalMap(const FString& InputPath, const FString& OutputPath, float Strength)
{
	FImage Source;
	if (!ComfyImage::Load(InputPath, Source))
	{
		return false;
	}

	const int32 W = Source.SizeX;
	const int32 H = Source.SizeY;
	TArrayView64<FLinearColor> In = Source.AsRGBA32F();

	TArray<float> Height;
	Height.SetNumUninitialized(W * H);
	for (int64 Index = 0; Index < int64(W) * H; ++Index)
	{
		Height[Index] = In[Index].GetLuminance();
	}

	auto At = [&](int32 X, int32 Y)
	{
		X = (X % W + W) % W;
		Y = (Y % H + H) % H;
		return Height[Y * W + X];
	};

	FImage Result(W, H, ERawImageFormat::RGBA32F, EGammaSpace::Linear);
	TArrayView64<FLinearColor> Out = Result.AsRGBA32F();
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const float Dx = (At(X + 1, Y - 1) + 2.f * At(X + 1, Y) + At(X + 1, Y + 1))
				- (At(X - 1, Y - 1) + 2.f * At(X - 1, Y) + At(X - 1, Y + 1));
			const float Dy = (At(X - 1, Y + 1) + 2.f * At(X, Y + 1) + At(X + 1, Y + 1))
				- (At(X - 1, Y - 1) + 2.f * At(X, Y - 1) + At(X + 1, Y - 1));

			// Image rows run downward, which matches Unreal's green-down convention.
			FVector3f Normal(-Dx * Strength / 8.f, Dy * Strength / 8.f, 1.f);
			Normal.Normalize();
			Out[int64(Y) * W + X] = FLinearColor(Normal.X * 0.5f + 0.5f, Normal.Y * 0.5f + 0.5f, Normal.Z * 0.5f + 0.5f, 1.f);
		}
	}

	return ComfyImage::Save(OutputPath, Result, EGammaSpace::Linear);
}

int64 UComfyRuntimeLibrary::NewSeed()
{
	return (int64(FMath::Rand()) << 31 | int64(FMath::Rand())) & 0xFFFFFFFFFFFFLL;
}
