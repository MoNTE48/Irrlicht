#include <irrlicht.h>

using namespace irr;

#ifdef _MSC_VER
#pragma comment(lib, "Irrlicht.lib")
#endif

int main(int argc, char *argv[])
{
	core::dimension2d<u32> dim(320,200);
	IrrlichtDevice * Device = createDevice(video::EDT_OGLES2, dim);
	if (!Device)
		return false;

    video::IVideoDriver* videoDriver =  Device->getVideoDriver();

    int offsetX = 0;	// just for experimenting
    int offsetY = 0;
	while ( Device->run() )
	{
		if ( Device->isWindowActive() )
		{
			videoDriver->beginScene(true, true);

			for ( int i=0; i<dim.Width; i+=4)
			{
				videoDriver->draw2DRectangleOutline( core::recti(offsetX+i,offsetY+0,offsetX+i,offsetY+9), video::SColor(255, 255, 0, 0));
				videoDriver->draw2DRectangleOutline( core::recti(offsetX+i+2,offsetY+0,offsetX+i+2,offsetY+9), video::SColor(255, 0, 255, 0));
			}

#if 1
			for ( int i=0; i<dim.Height; i+=4)
			{
				videoDriver->draw2DRectangleOutline( core::recti(offsetX+50,offsetY+i,offsetX+59,offsetY+i), video::SColor(255, 255, 0, 0));
				videoDriver->draw2DRectangleOutline( core::recti(offsetX+50,offsetY+i+2,offsetX+59,offsetY+i+2), video::SColor(255, 0, 255, 0));
			}
#endif

			videoDriver->endScene();
		}
		Device->yield();
	}

	Device->closeDevice();
	Device->drop();

	return 0;
}
