#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include "ARLib\Webcam\VideoPlayer.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static const char *destinationDir;
static bool done = false;
static int captureIndex = 0;
static ARLib::VideoPlayer *leftPlayer, *rightPlayer;
static int width, height;
static void *memL, *memR;

static bool directoryExists(const char *path)
{
	DWORD attribs = GetFileAttributesA(path);
	return (attribs != INVALID_FILE_ATTRIBUTES && (attribs & FILE_ATTRIBUTE_DIRECTORY));
}

static int recursiveDelete(const char *path)
{
    char dir[MAX_PATH + 1];
    SHFILEOPSTRUCTA fos = {0};

    strncpy(dir, path, MAX_PATH);
    dir[strlen(dir) + 1] = 0; // double null terminate for SHFileOperation

    // delete the folder and everything inside
    fos.wFunc = FO_DELETE;
    fos.pFrom = dir;
    fos.fFlags = FOF_NO_UI;
    return SHFileOperationA(&fos);
}

static void bgr2rgb(void *data, int width, int height)
{
	unsigned char *p = (unsigned char*)data;
	for (int i = 0; i < width * height; i++)
	{
		unsigned char tmp = p[0];
		p[0] = p[2];
		p[2] = tmp;
		p += 3;
	}
}

static LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
		case WM_PAINT:
		{
			PAINTSTRUCT paint;
			HDC DeviceContext = BeginPaint(window, &paint);
			// ignore
			EndPaint(window, &paint);
			return 0;
		} break;
		case WM_KEYDOWN:
		{
			if (wparam == VK_SPACE)
			{
				printf("capturing");

				captureIndex++;

				void *dataL = NULL, *dataR = NULL;
				while (!dataL || !dataR)
				{
					void *dL = leftPlayer->update();
					void *dR = rightPlayer->update();
					if (dL) { dataL = dL; memcpy(memL, dL, width * height * 3); }
					if (dR) { dataR = dR; memcpy(memR, dR, width * height * 3); }
				}

				printf(".");

				bgr2rgb(memL, width, height);
				bgr2rgb(memR, width, height);

				printf(".");

				char filename[MAX_PATH];
				_snprintf(filename, MAX_PATH, "%s\\capture_%003d_L.bmp", destinationDir, captureIndex);
				if(!stbi_write_bmp(filename, width, height, 3, memL))
				{
					fprintf(stderr, "could not save \"%s\"\n", filename);
					done = true;
				}

				printf(".");

				_snprintf(filename, MAX_PATH, "%s\\capture_%003d_R.bmp", destinationDir, captureIndex);
				if(!stbi_write_bmp(filename, width, height, 3, memR))
				{
					fprintf(stderr, "could not save \"%s\"\n", filename);
					done = true;
				}

				printf("saved capture %003d\n", captureIndex);
				return 0;
			}
			else if (wparam == VK_ESCAPE)
			{
				done = true;
				return 0;
			}
		} break;
		case WM_CLOSE:
		{
			done = true;
			return 0;
		} break;
		default:
			return DefWindowProcA(window, msg, wparam, lparam);
	}
}

int main(int argc, char **argv)
{
	destinationDir = argv[1];

	if (argc < 2)
	{
		printf("usage: %s <destination dir>\n", argv[0]);
		printf("using default destination directory \"calibration\"\n", argv[0]);
		destinationDir = "calibration";
	}

	if (directoryExists(destinationDir))
	{
		printf("the destination directory does already exist\n");
		printf("do you want to overwrite the contents? y/n\n");
		char c = getchar();
		if (c != 'y')
			return 0;

		if (recursiveDelete(destinationDir))
		{
			fprintf(stderr, "could not clean destination directory \"%s\"\n", destinationDir);
			return -1;
		}
	}

	if (!CreateDirectoryA(destinationDir, NULL))
	{
		fprintf(stderr, "could not create destination directory \"%s\"\n", destinationDir);
		return -1;
	}

	// video initialization
	leftPlayer = new ARLib::VideoPlayer(0);
	rightPlayer = new ARLib::VideoPlayer(1);

	assert(leftPlayer->getVideoWidth() == rightPlayer->getVideoWidth() &&
		   leftPlayer->getVideoHeight() == rightPlayer->getVideoHeight());

	width = leftPlayer->getVideoWidth();
	height = rightPlayer->getVideoHeight();

	memL = new unsigned char[width * height * 3];
	memR = new unsigned char[width * height * 3];

	// window
	WNDCLASSA windowClass = {};
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = GetModuleHandle(NULL);
	windowClass.lpszClassName = "VideoWindowClass";

	if (!RegisterClassA(&windowClass))
	{
		fprintf(stderr, "could not create window class\n");
		return -1;
	}

	HWND window = CreateWindowExA(
		0, windowClass.lpszClassName, "Live Capture", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT, 1280, 480, 0, 0, windowClass.hInstance, 0);

	if (!window)
	{
		fprintf(stderr, "could not create window\n");
		return -1;
	}

	// bitmap
	BITMAPINFO bmp = {};
	bmp.bmiHeader.biSize = sizeof(bmp.bmiHeader);
    bmp.bmiHeader.biWidth = width;
    bmp.bmiHeader.biHeight = height;
    bmp.bmiHeader.biPlanes = 1;
    bmp.bmiHeader.biBitCount = 24;
    bmp.bmiHeader.biCompression = BI_RGB;


	printf("press SPACE to capture or ESCAPE to quit\n");

	while(!done)
	{
		// message handling
		MSG msg;
		while (PeekMessageA(&msg, window, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
		}

		// video display
		HDC dc = GetDC(window);
		SetStretchBltMode(dc, HALFTONE);
		void *streamL = leftPlayer->update();
		if (streamL)
			StretchDIBits(dc, 0, 0, 640, 480, 0, 0, width, height, streamL, &bmp, DIB_RGB_COLORS, SRCCOPY);
		void *streamR = rightPlayer->update();
		if (streamR)
			StretchDIBits(dc, 640, 0, 640, 480, 0, 0, width, height, streamR, &bmp, DIB_RGB_COLORS, SRCCOPY);
		ReleaseDC(window, dc);
	}

	delete[] memL;
	delete[] memR;
	delete leftPlayer;
	delete rightPlayer;

	return 0;
}