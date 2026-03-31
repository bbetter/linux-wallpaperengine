#pragma once

#include "../Utils/TypeCaster.h"
#include "DynamicValue.h"
#include <map>
#include <sstream>
#include <string>
#include <utility>

namespace WallpaperEngine::Data::Model {
using namespace WallpaperEngine::Data::Utils;
using namespace WallpaperEngine::Data::Builders;

struct PropertyData {
    std::string name;
    std::string text;
};

struct SliderData {
    float min;
    float max;
    float step;
};

struct ComboData {
    std::map<std::string, std::string> values;
};

class Property : public DynamicValue, public TypeCaster, public PropertyData {
public:
    explicit Property (PropertyData data) : DynamicValue (), TypeCaster (), PropertyData (std::move (data)) { }

    using DynamicValue::update;
    virtual void update (const std::string& value) = 0;
    [[nodiscard]] virtual std::string dump () const = 0;
};

class PropertySlider final : public Property, SliderData {
public:
    PropertySlider (PropertyData data, SliderData sliderData, const float value) :
	Property (std::move (data)), SliderData (std::move (sliderData)) {
	this->Property::update (value);
    }

    using Property::update;
    void update (const std::string& value) override { this->update (std::stof (value)); }

    [[nodiscard]] std::string dump () const override {
	std::stringstream ss;

	ss << this->name << " - slider" << std::endl
	   << "\tText: " << this->text << std::endl
	   << "\tMin: " << this->min << std::endl
	   << "\tMax: " << this->max << std::endl
	   << "\tStep: " << this->step << std::endl
	   << "\tValue: " << this->toString () << std::endl;

	return ss.str ();
    }
};

class PropertyBoolean final : public Property {
public:
    explicit PropertyBoolean (PropertyData data, const bool value) : Property (std::move (data)) {
	this->Property::update (value);
    }

    using Property::update;
    void update (const std::string& value) override { this->update (value == "true" || value == "1"); }

    [[nodiscard]] std::string dump () const override {
	std::stringstream ss;

	ss << this->name << " - boolean" << std::endl
	   << "\tText: " << this->text << std::endl
	   << "\tValue: " << this->toString () << std::endl;

	return ss.str ();
    }
};

class PropertyColor final : public Property {
public:
    explicit PropertyColor (PropertyData data, const std::string& value) : Property (std::move (data)) {
	this->PropertyColor::update (value);
    }

    using Property::update;
    void update (const std::string& value) override {
	auto copy = value;

	// TODO: ENSURE ALL THIS PARSING IS CORRECT
	if (copy.find (',') != std::string::npos) {
	    // replace comma separator with spaces so it's
	    std::ranges::replace (copy, ',', ' ');
	}

	// hex colors should be converted to int colors
	if (copy.find ('#') == 0) {
	    auto number = copy.substr (1);

	    // support for css notation
	    if (number.size () == 3) {
		number = number[0] + number[0] + number[1] + number[1] + number[2] + number[2];
	    }

	    float alpha = 1.0f;
	    if (number.size () > 6) {
		const auto alphaHex = number.substr (number.size () - 2);
		alpha = std::stoi (alphaHex, nullptr, 16) / 255.0f;
		number = number.substr (0, 6);
	    }

	    const auto color = std::stoi (number, nullptr, 16);

	    // format the number as float vector
	    copy = std::to_string (((color >> 16) & 0xFF) / 255.0) + " "
		+ std::to_string (((color >> 8) & 0xFF) / 255.0) + " "
		+ std::to_string ((color & 0xFF) / 255.0) + " "
		+ std::to_string (alpha);
	} else if (copy.find ('.') == std::string::npos) {
	    // integer vector, convert it to float vector; optionally parse 4th alpha component
	    std::istringstream ss (copy);
	    int r, g, b, a = 255;
	    ss >> r >> g >> b;
	    if (!(ss >> a)) a = 255;

	    copy = std::to_string (r / 255.0) + " " + std::to_string (g / 255.0) + " "
		+ std::to_string (b / 255.0) + " " + std::to_string (a / 255.0);
	} else if (VectorBuilder::preparseSize (copy) == 3) {
	    // float RGB vector without alpha — append alpha=1.0
	    copy += " 1.000000";
	}

	// finally parse the string as a float vector
	this->update (VectorBuilder::parse<glm::vec4> (copy));
    }

    [[nodiscard]] std::string dump () const override {
	std::stringstream ss;

	ss << this->name << " - color" << std::endl
	   << "\tText: " << this->text << std::endl
	   << "\tValue: " << this->toString () << std::endl;

	return ss.str ();
    }
};

class PropertyCombo final : public Property, ComboData {
public:
    PropertyCombo (PropertyData data, ComboData comboData, const std::string& value) :
	Property (std::move (data)), ComboData (std::move (comboData)) {
	this->PropertyCombo::update (value);
    }

    using Property::update;
    void update (const std::string& value) override {
	if (this->values.contains (value) == false) {
	    sLog.error ("Combo value not found in combo options: ", value);
	    return;
	}

	// search for the value in the combo options or default to the textual value
	this->DynamicValue::update (value);
    }

    [[nodiscard]] std::string dump () const override {
	std::stringstream ss;

	ss << this->name << " - combo" << std::endl
	   << "\tText: " << this->text << std::endl
	   << "\tValue: " << this->toString () << std::endl
	   << "Values: " << std::endl;

	for (const auto& [key, value] : this->values) {
	    ss << "\t\t" << key << " = " << value << std::endl;
	}

	return ss.str ();
    }
};

class PropertyText final : public Property {
public:
    explicit PropertyText (PropertyData data) : Property (std::move (data)) { }

    using Property::update;
    void update (const std::string& value) override {
	throw std::runtime_error ("PropertyText::update() is not implemented");
    }

    [[nodiscard]] std::string toString () const override { return this->text; }

    [[nodiscard]] std::string dump () const override {
	std::stringstream ss;

	ss << this->name << " - text" << std::endl
	   << "\tText: " << this->text << std::endl
	   << "\tValue: " << this->toString () << std::endl;

	return ss.str ();
    }
};

class PropertySceneTexture final : public Property {
public:
    explicit PropertySceneTexture (PropertyData data, const std::string& value) : Property (std::move (data)) {
	this->PropertySceneTexture::update (value);
    }

    void update (const std::string& value) override { this->m_value = value; }

    [[nodiscard]] std::string dump () const override {
	std::stringstream ss;

	ss << this->name << " - scene texture" << std::endl
	   << "\tText: " << this->text << std::endl
	   << "\tValue: " << this->m_value << std::endl;

	return ss.str ();
    }

private:
    std::string m_value;
};

class PropertyFile final : public Property {
public:
    explicit PropertyFile (PropertyData data, const std::string& value) : Property (std::move (data)) {
	this->PropertyFile::update (value);
    }

    void update (const std::string& value) override { this->m_value = value; }

    [[nodiscard]] std::string dump () const override {
	std::stringstream ss;

	ss << this->name << " - file" << std::endl
	   << "\tText: " << this->text << std::endl
	   << "\tValue: " << this->m_value << std::endl;

	return ss.str ();
    }

private:
    std::string m_value;
};

class PropertyTextInput final : public Property {
public:
    explicit PropertyTextInput (PropertyData data, const std::string& value) : Property (std::move (data)) {
	this->PropertyTextInput::update (value);
    }

    void update (const std::string& value) override { this->m_value = value; }

    [[nodiscard]] std::string dump () const override {
	std::stringstream ss;

	ss << this->name << " - textinput" << std::endl
	   << "\tText: " << this->text << std::endl
	   << "\tValue: " << this->m_value << std::endl;

	return ss.str ();
    }

private:
    std::string m_value;
};
}