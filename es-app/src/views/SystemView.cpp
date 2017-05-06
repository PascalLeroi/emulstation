#include "views/SystemView.h"
#include "SystemData.h"
#include "Renderer.h"
#include "Log.h"
#include "Window.h"
#include "views/ViewController.h"
#include "animations/LambdaAnimation.h"
#include "SystemData.h"
#include "Settings.h"
#include "Util.h"

// buffer values for scrolling velocity (left, stopped, right)
const int logoBuffersLeft[] = { -5, -2, -1 };
const int logoBuffersRight[] = { 1, 2, 5 };

SystemView::SystemView(Window* window) : IList<SystemViewData, SystemData*>(window, LIST_SCROLL_STYLE_SLOW, LIST_ALWAYS_LOOP),
										 mViewNeedsReload(true),
										 mSystemInfo(window, "SYSTEM INFO", Font::get(FONT_SIZE_SMALL), 0x33333300, ALIGN_CENTER)
{
	mCamOffset = 0;
	mExtrasCamOffset = 0;
	mExtrasFadeOpacity = 0.0f;

	setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
	populate();
}

void SystemView::populate()
{
	mEntries.clear();

	for(auto it = SystemData::sSystemVector.begin(); it != SystemData::sSystemVector.end(); it++)
	{
		const std::shared_ptr<ThemeData>& theme = (*it)->getTheme();

		if(mViewNeedsReload)
			getViewElements(theme);

		Entry e;
		e.name = (*it)->getName();
		e.object = *it;

		// make logo
		if(theme->getElement("system", "logo", "image"))
		{
			ImageComponent* logo = new ImageComponent(mWindow, false, false);
			logo->setMaxSize(Eigen::Vector2f(mCarousel.logoSize.x() * mCarousel.logoScale, mCarousel.logoSize.y() * mCarousel.logoScale));
			logo->applyTheme((*it)->getTheme(), "system", "logo", ThemeFlags::PATH | ThemeFlags::COLOR);

			if (mCarousel.type == VERTICAL)
			{
				if (mCarousel.logoAlignment == LEFT)
					logo->setOrigin(0, 0.5);
				else if (mCarousel.logoAlignment == RIGHT)
					logo->setOrigin(1.0, 0.5);
				else
					logo->setOrigin(0.5, 0.5);
			} else {
				if (mCarousel.logoAlignment == TOP)
					logo->setOrigin(0.5, 0);
				else if (mCarousel.logoAlignment == BOTTOM)
					logo->setOrigin(0.5, 1);
				else
					logo->setOrigin(0.5, 0.5);
			}

			logo->setPosition(mCarousel.logoSize.x() * logo->getOrigin().x(),
					mCarousel.logoSize.y() * logo->getOrigin().y());

			e.data.logo = std::shared_ptr<GuiComponent>(logo);
		}else{
			// no logo in theme; use text
			TextComponent* text = new TextComponent(mWindow, 
				(*it)->getName(), 
				Font::get(FONT_SIZE_LARGE), 
				0x000000FF, 
				ALIGN_CENTER);
			text->setSize(mCarousel.logoSize * mCarousel.logoScale);

			e.data.logo = std::shared_ptr<GuiComponent>(text);
		}

		// make background extras
		e.data.backgroundExtras = std::shared_ptr<ThemeExtras>(new ThemeExtras(mWindow));
		e.data.backgroundExtras->setExtras(ThemeData::makeExtras((*it)->getTheme(), "system", mWindow));

		this->add(e);
	}
}

void SystemView::goToSystem(SystemData* system, bool animate)
{
	setCursor(system);

	if(!animate)
		finishAnimation(0);
}

bool SystemView::input(InputConfig* config, Input input)
{
	if(input.value != 0)
	{
		if(config->getDeviceId() == DEVICE_KEYBOARD && input.value && input.id == SDLK_r && SDL_GetModState() & KMOD_LCTRL && Settings::getInstance()->getBool("Debug"))
		{
			LOG(LogInfo) << " Reloading all";
			ViewController::get()->reloadAll();
			return true;
		}

		switch (mCarousel.type)
		{
		case VERTICAL:
			if (config->isMappedTo("up", input))
			{
				listInput(-1);
				return true;
			}
			if (config->isMappedTo("down", input))
			{
				listInput(1);
				return true;
			}
			break;
		case HORIZONTAL:
		default:
			if (config->isMappedTo("left", input))
			{
				listInput(-1);
				return true;
			}
			if (config->isMappedTo("right", input))
			{
				listInput(1);
				return true;
			}
			break;
		}

		if(config->isMappedTo("a", input))
		{
			stopScrolling();
			ViewController::get()->goToGameList(getSelected());
			return true;
		}
		if (config->isMappedTo("x", input))
		{
			ViewController::get()->goToRandomGame();
			return true;
		}
	}else{
		if(config->isMappedTo("left", input) || 
			config->isMappedTo("right", input) ||
			config->isMappedTo("up", input) || 
			config->isMappedTo("down", input))
			listInput(0);
	}

	return GuiComponent::input(config, input);
}

void SystemView::update(int deltaTime)
{
	listUpdate(deltaTime);
	GuiComponent::update(deltaTime);
}

void SystemView::onCursorChanged(const CursorState& state)
{
	// update help style
	updateHelpPrompts();

	float startPos = mCamOffset;

	float posMax = (float)mEntries.size();
	float target = (float)mCursor;

	// what's the shortest way to get to our target?
	// it's one of these...

	float endPos = target; // directly
	float dist = abs(endPos - startPos);
	
	if(abs(target + posMax - startPos) < dist)
		endPos = target + posMax; // loop around the end (0 -> max)
	if(abs(target - posMax - startPos) < dist)
		endPos = target - posMax; // loop around the start (max - 1 -> -1)

	
	// animate mSystemInfo's opacity (fade out, wait, fade back in)

	cancelAnimation(1);
	cancelAnimation(2);

	const float infoStartOpacity = mSystemInfo.getOpacity() / 255.f;

	Animation* infoFadeOut = new LambdaAnimation(
		[infoStartOpacity, this] (float t)
	{
		mSystemInfo.setOpacity((unsigned char)(lerp<float>(infoStartOpacity, 0.f, t) * 255));
	}, (int)(infoStartOpacity * 150));

	unsigned int gameCount = getSelected()->getGameCount();

	// also change the text after we've fully faded out
	setAnimation(infoFadeOut, 0, [this, gameCount] {
		std::stringstream ss;
		
		if (getSelected()->getName() == "retropie")
			ss << "CONFIGURATION";
		// only display a game count if there are at least 2 games
		else if(gameCount > 1)
			ss << gameCount << " GAMES AVAILABLE";

		mSystemInfo.setText(ss.str()); 
	}, false, 1);

	// only display a game count if there are at least 2 games
	if(gameCount > 1)
	{
		Animation* infoFadeIn = new LambdaAnimation(
			[this](float t)
		{
			mSystemInfo.setOpacity((unsigned char)(lerp<float>(0.f, 1.f, t) * 255));
		}, 300);

		// wait 600ms to fade in
		setAnimation(infoFadeIn, 2000, nullptr, false, 2);
	}

	// no need to animate transition, we're not going anywhere (probably mEntries.size() == 1)
	if(endPos == mCamOffset && endPos == mExtrasCamOffset)
		return;

	Animation* anim;
	if(Settings::getInstance()->getString("TransitionStyle") == "fade")
	{
		float startExtrasFade = mExtrasFadeOpacity;
		anim = new LambdaAnimation(
			[startExtrasFade, startPos, endPos, posMax, this](float t)
		{
			t -= 1;
			float f = lerp<float>(startPos, endPos, t*t*t + 1);
			if(f < 0)
				f += posMax;
			if(f >= posMax)
				f -= posMax;

			this->mCamOffset = f;

			t += 1;
			if(t < 0.3f)
				this->mExtrasFadeOpacity = lerp<float>(0.0f, 1.0f, t / 0.3f + startExtrasFade);
			else if(t < 0.7f)
				this->mExtrasFadeOpacity = 1.0f;
			else
				this->mExtrasFadeOpacity = lerp<float>(1.0f, 0.0f, (t - 0.7f) / 0.3f);

			if(t > 0.5f)
				this->mExtrasCamOffset = endPos;

		}, 500);
	}
	else{ // slide
		anim = new LambdaAnimation(
			[startPos, endPos, posMax, this](float t)
		{
			t -= 1;
			float f = lerp<float>(startPos, endPos, t*t*t + 1);
			if(f < 0)
				f += posMax;
			if(f >= posMax)
				f -= posMax;

			this->mCamOffset = f;
			this->mExtrasCamOffset = f;
		}, 500);
	}

	setAnimation(anim, 0, nullptr, false, 0);
}

void SystemView::render(const Eigen::Affine3f& parentTrans)
{
	if(size() == 0)
		return;  // nothing to render
	
	Eigen::Affine3f trans = getTransform() * parentTrans;

	renderExtras(trans);
	renderCarousel(trans);
	renderInfoBar(trans);
}

std::vector<HelpPrompt> SystemView::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	if (mCarousel.type == VERTICAL)
		prompts.push_back(HelpPrompt("up/down", "choose"));
	else
		prompts.push_back(HelpPrompt("left/right", "choose"));
	prompts.push_back(HelpPrompt("a", "select"));
	prompts.push_back(HelpPrompt("x", "random"));
	return prompts;
}

HelpStyle SystemView::getHelpStyle()
{
	HelpStyle style;
	style.applyTheme(mEntries.at(mCursor).object->getTheme(), "system");
	return style;
}

void  SystemView::onThemeChanged(const std::shared_ptr<ThemeData>& theme)
{
	LOG(LogDebug) << "SystemView::onThemeChanged()";
	mViewNeedsReload = true;
	populate();
}

//  Get the ThemeElements that make up the SystemView.
void  SystemView::getViewElements(const std::shared_ptr<ThemeData>& theme)
{
	LOG(LogDebug) << "SystemView::getViewElements()";

	getDefaultElements();

	if (!theme->hasView("system"))
		return;

	const ThemeData::ThemeElement* carouselElem = theme->getElement("system", "systemcarousel", "carousel");
	if (carouselElem)
		getCarouselFromTheme(carouselElem);

	const ThemeData::ThemeElement* sysInfoElem = theme->getElement("system", "systemInfo", "text");
	if (sysInfoElem)
		mSystemInfo.applyTheme(theme, "system", "systemInfo", ThemeFlags::ALL);

	mViewNeedsReload = false;
}

//  Render system carousel
void SystemView::renderCarousel(const Eigen::Affine3f& trans)
{
	//Eigen::Vector2i clipPos((int)mCarousel.pos.x(), (int)mCarousel.pos.y());
	//Eigen::Vector2i clipSize((int)mCarousel.size.x(), (int)mCarousel.size.y());

	//Renderer::pushClipRect(clipPos, clipSize);
	Renderer::pushClipRect(mCarousel.pos.cast<int>(), mCarousel.size.cast<int>());

	// background box behind logos

	Eigen::Affine3f carouselTrans = trans;
	carouselTrans.translate(Eigen::Vector3f(mCarousel.pos.x(), mCarousel.pos.y(), 0.0));

	Renderer::setMatrix(carouselTrans);
	Renderer::drawRect(0.0, 0.0, mCarousel.size.x(), mCarousel.size.y(), mCarousel.color);

	// draw logos
	Eigen::Vector2f logoSpacing(0.0, 0.0); // NB: logoSpacing will include the size of the logo itself as well!
	float xOff = 0.0;
	float yOff = 0.0;

	switch (mCarousel.type)
	{
		case VERTICAL:
			logoSpacing[1] = ((mCarousel.size.y() - (mCarousel.logoSize.y() * mCarousel.maxLogoCount)) / (mCarousel.maxLogoCount)) + mCarousel.logoSize.y();
			yOff = (mCarousel.size.y() - mCarousel.logoSize.y()) / 2 - (mCamOffset * logoSpacing[1]);

			if (mCarousel.logoAlignment == LEFT)
				xOff = mCarousel.logoSize.x() / 10;
			else if (mCarousel.logoAlignment == RIGHT)
				xOff = mCarousel.size.x() - (mCarousel.logoSize.x() * 1.1) ;
			else
				xOff = (mCarousel.size.x() - mCarousel.logoSize.x()) / 2;
			break;
		case HORIZONTAL:
		default:
			logoSpacing[0] = ((mCarousel.size.x() - (mCarousel.logoSize.x() * mCarousel.maxLogoCount)) / (mCarousel.maxLogoCount)) + mCarousel.logoSize.x();
			xOff = (mCarousel.size.x() - mCarousel.logoSize.x()) / 2 - (mCamOffset * logoSpacing[0]);

			if (mCarousel.logoAlignment == TOP)
				yOff = mCarousel.logoSize.y() / 10;
			else if (mCarousel.logoAlignment == BOTTOM)
				yOff = mCarousel.size.y() - (mCarousel.logoSize.y() * 1.1);
			else
				yOff = (mCarousel.size.y() - mCarousel.logoSize.y()) / 2;
			break;
	}

	//Eigen::Affine3f logoTrans = carouselTrans;
	int center = (int)(mCamOffset);
	int logoCount = std::min(mCarousel.maxLogoCount, (int)mEntries.size());

	// TODO: figure out how to make this work with bufferIndex
//	if (logoCount != 1)
//		logoCount += 2;

	// Adding texture loading buffers depending on scrolling speed and status	
	int bufferIndex = getScrollingVelocity() + 1;	
	
	for (int i = center - logoCount / 2 + logoBuffersLeft[bufferIndex]; i <= center + logoCount / 2 + logoBuffersRight[bufferIndex]; i++)
	{
		int index = i;
		while (index < 0)
			index += mEntries.size();
		while (index >= (int)mEntries.size())
			index -= mEntries.size();

		Eigen::Affine3f logoTrans = carouselTrans;
		logoTrans.translate(Eigen::Vector3f(i * logoSpacing[0] + xOff, i * logoSpacing[1] + yOff, 0));

		float distance = i - mCamOffset;

		float scale = 1.0 + ((mCarousel.logoScale - 1.0) * (1 - abs(distance)));
		scale = std::min(mCarousel.logoScale, std::max(1.0f, scale));
		scale /= mCarousel.logoScale;

		const std::shared_ptr<GuiComponent>& comp = mEntries.at(index).data.logo;
		comp->setRotation(mCarousel.logoRotation * distance);
		comp->setScale(scale);
		//comp->setOrigin(comp->getOrigin());

		if (index == mCursor) //Selected System
		{
			comp->setOpacity(0xFF);
		}
		else { // not selected systems
			comp->setOpacity(0x80);
		}
		comp->render(logoTrans);
	}
	Renderer::popClipRect();
}

void SystemView::renderInfoBar(const Eigen::Affine3f& trans)
{
	Renderer::setMatrix(trans);
	mSystemInfo.render(trans);
}

// Draw background extras
void SystemView::renderExtras(const Eigen::Affine3f& trans)
{
	int extrasCenter = (int)mExtrasCamOffset;
	
	// Adding texture loading buffers depending on scrolling speed and status
	int bufferIndex = getScrollingVelocity() + 1;

	Renderer::pushClipRect(Eigen::Vector2i(0, 0), mSize.cast<int>());
	for (int i = extrasCenter + logoBuffersLeft[bufferIndex]; i <= extrasCenter + logoBuffersRight[bufferIndex]; i++)
	{
		int index = i;
		while (index < 0)
			index += mEntries.size();
		while (index >= (int)mEntries.size())
			index -= mEntries.size();

		Eigen::Affine3f extrasTrans = trans;
		if (mCarousel.type == HORIZONTAL)
			extrasTrans.translate(Eigen::Vector3f((i - mExtrasCamOffset) * mSize.x(), 0, 0));
		else
			extrasTrans.translate(Eigen::Vector3f(0, (i - mExtrasCamOffset) * mSize.y(), 0));

		mEntries.at(index).data.backgroundExtras->render(extrasTrans);
	}
	Renderer::popClipRect();

	// fade extras if necessary
	if (mExtrasFadeOpacity)
	{
		Renderer::setMatrix(trans);
		Renderer::drawRect(0.0f, 0.0f, mSize.x(), mSize.y(), 0x00000000 | (unsigned char)(mExtrasFadeOpacity * 255));
	}
}

// Populate the system carousel with the legacy values
void  SystemView::getDefaultElements(void)
{
	// Carousel
	mCarousel.type = HORIZONTAL;
	mCarousel.logoAlignment = CENTER;
	mCarousel.size.x() = mSize.x();
	mCarousel.size.y() = 0.2325f * mSize.y();
	mCarousel.pos.x() = 0.0f;
	mCarousel.pos.y() = 0.5f * (mSize.y() - mCarousel.size.y());
	mCarousel.color = 0xFFFFFFD8;
	mCarousel.logoScale = 1.2f;
	mCarousel.logoRotation = 0;//.25f * M_PI;
	mCarousel.logoSize.x() = 0.25f * mSize.x();
	mCarousel.logoSize.y() = 0.155f * mSize.y();
	mCarousel.maxLogoCount = 3;

	// System Info Bar
	mSystemInfo.setSize(mSize.x(), mSystemInfo.getFont()->getLetterHeight()*2.2f);
	mSystemInfo.setPosition(0, (mCarousel.pos.y() + mCarousel.size.y() - 0.2f));
	mSystemInfo.setBackgroundColor(0xDDDDDDD8);
	mSystemInfo.setRenderBackground(true);
	mSystemInfo.setFont(Font::get((int)(0.035f * mSize.y()), Font::getDefaultPath()));
	mSystemInfo.setColor(0x000000FF);
}

void SystemView::getCarouselFromTheme(const ThemeData::ThemeElement* elem)
{
	if (elem->has("type"))
		mCarousel.type = !(elem->get<std::string>("type").compare("vertical")) ? VERTICAL : HORIZONTAL;
	if (elem->has("size"))
		mCarousel.size = elem->get<Eigen::Vector2f>("size").cwiseProduct(mSize);
	if (elem->has("pos"))
		mCarousel.pos = elem->get<Eigen::Vector2f>("pos").cwiseProduct(mSize);
	if (elem->has("color"))
		mCarousel.color = elem->get<unsigned int>("color");
	if (elem->has("logoScale"))
		mCarousel.logoScale = elem->get<float>("logoScale");
	if (elem->has("logoSize"))
		mCarousel.logoSize = elem->get<Eigen::Vector2f>("logoSize").cwiseProduct(mSize);
	if (elem->has("maxLogoCount"))
		mCarousel.maxLogoCount = std::round(elem->get<float>("maxLogoCount"));
	if (elem->has("logoRotation"))
		mCarousel.logoRotation = elem->get<float>("logoRotation") * M_PI / 180;
	if (elem->has("logoAlignment"))
	{
		if (!(elem->get<std::string>("logoAlignment").compare("left")))
			mCarousel.logoAlignment = LEFT;
		else if (!(elem->get<std::string>("logoAlignment").compare("right")))
			mCarousel.logoAlignment = RIGHT;
		else if (!(elem->get<std::string>("logoAlignment").compare("top")))
			mCarousel.logoAlignment = TOP;
		else if (!(elem->get<std::string>("logoAlignment").compare("bottom")))
			mCarousel.logoAlignment = BOTTOM;
		else
			mCarousel.logoAlignment = CENTER;
	}
}
