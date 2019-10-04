#include <neopop.h>
#include <emuframework/OptionView.hh>
#include <emuframework/EmuMainMenuView.hh>

class CustomSystemOptionView : public SystemOptionView
{
	BoolMenuItem ngpLanguage
	{
		"NGP Language",
		language_english,
		"Japanese", "English",
		[this](BoolMenuItem &item, View &, Input::Event e)
		{
			language_english = item.flipBoolValue(*this);
		}
	};

public:
	CustomSystemOptionView(ViewAttachParams attach): SystemOptionView{attach, true}
	{
		loadStockItems();
		item.emplace_back(&ngpLanguage);
	}
};

std::unique_ptr<View> EmuApp::makeCustomView(ViewAttachParams attach, ViewID id)
{
	switch(id)
	{
		case ViewID::SYSTEM_OPTIONS: return std::make_unique<CustomSystemOptionView>(attach);
		default: return nullptr;
	}
}
