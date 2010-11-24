#include "ReflectColorInterpreter.h"

#include "Foundation/Inspect/Container.h"
#include "Foundation/Inspect/Controls/LabelControl.h"
#include "Foundation/Inspect/Controls/SliderControl.h"
#include "Foundation/Inspect/Controls/ValueControl.h"
#include "Foundation/Inspect/Controls/ColorPickerControl.h"
#include "Foundation/Inspect/DataBinding.h"

using namespace Helium;
using namespace Helium::Reflect;
using namespace Helium::Inspect;

ReflectColorInterpreter::ReflectColorInterpreter( Container* container )
: ReflectFieldInterpreter( container )
{

}

void ReflectColorInterpreter::InterpretField( const Field* field, const std::vector<Reflect::Element*>& instances, Container* parent )
{
    ContainerPtr container = CreateControl< Container >();
    parent->AddChild( container );

    LabelPtr label = CreateControl< Label >();

    tstring temp;
    bool converted = Helium::ConvertString( field->m_UIName, temp );
    HELIUM_ASSERT( converted );

    label->BindText( temp );
    label->a_HelpText.Set( field->GetProperty( TXT( "HelpText" ) ) );

    container->AddChild( label );

    bool color3 = field->m_DataID == Reflect::GetType<Color3Data>() || field->m_DataID == Reflect::GetType<HDRColor3Data>();
    bool color4 = field->m_DataID == Reflect::GetType<Color4Data>() || field->m_DataID == Reflect::GetType<HDRColor4Data>();
    HELIUM_ASSERT( !(color3 && color4) ); // we shouldn't ever have both!

    if ( color3 || color4 )
    {
        std::vector<Data*> ser;
        std::vector<Reflect::Element*>::const_iterator itr = instances.begin();
        std::vector<Reflect::Element*>::const_iterator end = instances.end();
        for ( ; itr != end; ++itr )
        {
            DataPtr s;

            if ( color3 )
            {
                s = new Color3Data();
            }

            if ( color4 )
            {
                s = new Color4Data();
            }

            if (s.ReferencesObject())
            {
                s->ConnectField( *itr, field );
                ser.push_back( s );
                m_Datas.push_back( s );
            }
        }

        if ( !m_Datas.empty() )
        {
            ColorPickerPtr colorPicker = CreateControl<ColorPicker>();
            container->AddChild( colorPicker );

            colorPicker->a_HelpText.Set( field->GetProperty( TXT( "HelpText" ) ) );

            bool readOnly = ( field->m_Flags & FieldFlags::ReadOnly ) == FieldFlags::ReadOnly;
            colorPicker->a_IsReadOnly.Set( readOnly );

            DataBindingPtr data = new MultiStringFormatter<Data>( ser );
            colorPicker->Bind( data );

            if ( color3 )
            {
                colorPicker->a_Alpha.Set( false );
            }

            if ( color4 )
            {
                colorPicker->a_Alpha.Set( true );

                SliderPtr slider = CreateControl<Slider>();
                container->AddChild( slider );
                slider->a_Min.Set( 0.0 );
                slider->a_Max.Set( 255.0f );
                slider->a_IsReadOnly.Set( readOnly );
                slider->a_HelpText.Set( TXT( "Sets the alpha value for the color." ) );

                ValuePtr value = CreateControl<Value>();
                container->AddChild( value );
                value->a_IsReadOnly.Set( readOnly );
                value->a_HelpText.Set( TXT( "Sets the alpha value for the color." ) );

                std::vector<Data*> alphaSer;
                std::vector<Reflect::Element*>::const_iterator itr = instances.begin();
                std::vector<Reflect::Element*>::const_iterator end = instances.end();
                for ( ; itr != end; ++itr )
                {
                    DataPtr s = new U8Data ();

                    uintptr_t fieldAddress = (uintptr_t)(*itr) + field->m_Offset;

                    Color4* col = (Color4*)fieldAddress;
                    intptr_t offsetInField = (intptr_t)( &col->a ) - fieldAddress;
                    s->ConnectField( *itr, field, offsetInField );

                    alphaSer.push_back( s );

                    m_Datas.push_back( s );
                }

                data = new MultiStringFormatter<Data>( alphaSer );
                slider->Bind( data );
                value->Bind( data );
            }

            if ( field->m_DataID == Reflect::GetType<HDRColor3Data>() || field->m_DataID == Reflect::GetType<HDRColor4Data>() )
            {
                SliderPtr slider = CreateControl<Slider>();
                container->AddChild( slider );
                slider->a_Min.Set( 0.0 );
                slider->a_Max.Set( 8.0 );
                slider->a_IsReadOnly.Set( readOnly );
                slider->a_HelpText.Set( TXT( "Adjusts the HDR value of the color." ) );

                ValuePtr value = CreateControl<Value>();
                container->AddChild( value );
                value->a_IsReadOnly.Set( readOnly );
                value->a_HelpText.Set( TXT( "Adjusts the HDR value of the color." ) );

                std::vector<Data*> intensitySer;
                std::vector<Reflect::Element*>::const_iterator itr = instances.begin();
                std::vector<Reflect::Element*>::const_iterator end = instances.end();
                for ( ; itr != end; ++itr )
                {
                    DataPtr s = new F32Data();

                    uintptr_t fieldAddress = (uintptr_t)(*itr) + field->m_Offset;

                    if ( color3 )
                    {
                        HDRColor3* col = (HDRColor3*)fieldAddress;
                        intptr_t offsetInField = (intptr_t)( &col->s ) - fieldAddress;
                        s->ConnectField( *itr, field, offsetInField );
                    }

                    if ( color4 )
                    {
                        HDRColor4* col = (HDRColor4*)fieldAddress;
                        intptr_t offsetInField = (intptr_t)( &col->s ) - fieldAddress;
                        s->ConnectField( *itr, field, offsetInField );
                    }

                    intensitySer.push_back( s );

                    m_Datas.push_back( s );
                }

                data = new MultiStringFormatter<Data>( intensitySer );
                slider->Bind( data );
                value->Bind( data );
            }
        }
    }
}
