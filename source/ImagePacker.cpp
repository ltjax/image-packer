#include <boost/algorithm/string/predicate.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <fmt/format.h>
#include <fstream>
#include <iostream>
#include <replay/box_packer.hpp>
#include <replay/pixbuf.hpp>
#include <replay/pixbuf_io.hpp>
#include <replay/v2.hpp>
#include <tuple>

using KeyValueList = std::vector<std::pair<std::string, std::string>>;

struct ImageEntryType
{
    ImageEntryType()
    : IsNinePatch(false)
    {
    }

    std::filesystem::path RelativePath;
    replay::pixbuf Image;
    replay::box<int> Box;

    bool IsNinePatch;

    replay::box<int> ScaleableArea;
    replay::box<int> FillArea;
};

std::tuple<unsigned int, unsigned int> AnalyzeLine(replay::pixbuf const& Image, unsigned int Offset, unsigned int Axis)
{
    // This needs RGBA
    assert(Image.channel_count() == 4);

    replay::vector2i Coord(0);
    Coord[Axis ^ 1] = Offset;

    replay::vector2i Delta(0);
    Delta[Axis] = 1;

    // Alias the size of the image for brevity
    auto const w = static_cast<int>(Image.width());
    auto const h = static_cast<int>(Image.height());

    // Move to the beginning
    while (Coord[0] < w && Coord[1] < h && Image.ptr(Coord[0], Coord[1])[3] == 0)
        Coord += Delta;

    // Mark the beginning of the black line
    unsigned int Begin = Coord[Axis];

    unsigned char Black[] = { 0, 0, 0, 255 };
    while (Coord[0] < w && Coord[1] < h && std::equal(Black, Black + 4, Image.ptr(Coord[0], Coord[1])))
        Coord += Delta;

    // Mark the end of the black line
    unsigned int End = Coord[Axis];

    while (Coord[0] < w && Coord[1] < h && Image.ptr(Coord[0], Coord[1])[3] == 0)
        Coord += Delta;

    // Check if we reached the end
    if (Coord[0] != w && Coord[1] != h)
        throw std::invalid_argument("Invalid black-line size specifier.");

    // Subtract 1 because of the added border (results need to be relative to the cropped image)
    return std::make_tuple(Begin - 1, End - 1);
}

void AddFile(std::vector<ImageEntryType>& List,
             std::filesystem::path const& FilePath,
             std::filesystem::path const& RelativeFilePath)
{
    // Check if this is a supported image format
    if (FilePath.extension() != ".png")
        return;

    std::cout << "Loading " << FilePath.string() << std::endl;

    auto Image = replay::pixbuf_io::load_from_file(FilePath);
    ImageEntryType Entry;
    Entry.RelativePath = RelativeFilePath;

    // Check if this is a 9-patch
    if (boost::algorithm::ends_with(FilePath.filename().string(), ".9.png"))
    {
        auto w = Image.width(), h = Image.height();
        if (Image.channel_count() != 4)
            throw std::invalid_argument("9-patch must have alpha channel");

        if (w < 4 || h < 4)
            throw std::invalid_argument("9-patch images must be at least 4x4");

        // Analyze top and left
        auto ScalableX = AnalyzeLine(Image, h - 1, 0);
        auto ScalableY = AnalyzeLine(Image, 0, 1);

        // Analyze bottom and right
        auto FillX = AnalyzeLine(Image, 0, 0);
        auto FillY = AnalyzeLine(Image, w - 1, 1);

        if (std::get<0>(FillX) == std::get<1>(FillX))
            FillX = ScalableX;

        if (std::get<0>(FillY) == std::get<1>(FillY))
            FillY = ScalableY;

        Entry.ScaleableArea.set(std::get<0>(ScalableX), std::get<0>(ScalableY), std::get<1>(ScalableX),
                                std::get<1>(ScalableY));

        Entry.FillArea.set(std::get<0>(FillX), std::get<0>(FillY), std::get<1>(FillX), std::get<1>(FillY));

        // Extract the actual image data
        Entry.Image = Image.crop(1, 1, w - 2, h - 2);

        Entry.IsNinePatch = true;
    }
    else
    {
        Entry.Image = Image;
    }

    List.push_back(Entry);
}

void ScanFile(std::vector<ImageEntryType>& List, std::filesystem::path const& Path)
{
    if (!is_directory(Path))
    {
        AddFile(List, Path, Path.filename());
        return;
    }

    using namespace std::filesystem;
    using Iterator = std::filesystem::recursive_directory_iterator;
    std::size_t BaseOffset = std::distance(Path.begin(), Path.end());
    for (Iterator i(Path), ie; i != ie; ++i)
    {
        if (is_symlink(*i))
            i.disable_recursion_pending();

        if (!is_regular_file(*i))
            continue;

        path Absolute = *i;
        path Relative;
        auto j = Absolute.begin();

        std::size_t c = 0;
        for (; j != Absolute.end(); ++j, ++c)
            if (c >= BaseOffset)
                Relative /= *j;

        if (!Relative.empty())
            AddFile(List, Path / Relative, Relative);
    }
}

bool PackInto(std::vector<ImageEntryType>& List, int Width, int Height)
{
    replay::box_packer Packer(Width, Height);

    for (auto& Each : List)
        if (!Packer.pack(Each.Image.width(), Each.Image.height(), &Each.Box))
            return false;

    return true;
}

void BlitImages(replay::pixbuf& Result, std::vector<ImageEntryType>& List)
{
    for (auto& Entry : List)
    {
        Entry.Image.convert_to_rgba();
        Result.blit_from(Entry.Box.left, Entry.Box.bottom, Entry.Image);
    }
}

void PackImages(std::filesystem::path const& ResultImage, std::vector<ImageEntryType>& List)
{
    // Start by trying to open the result file
    std::ofstream File(ResultImage, std::ios::binary | std::ios::trunc);

    if (!File.good())
        throw std::runtime_error("Unable to open target file: " + ResultImage.string());

    // Use an appropriate starting size
    int PixelCount = 0;
    int MinWidth = 0;
    int MinHeight = 0;

    for (auto const& Entry : List)
    {
        auto const& Image(Entry.Image);
        MinWidth = std::max<int>(MinWidth, Image.width());
        MinHeight = std::max<int>(MinHeight, Image.height());
        PixelCount += Image.width() * Image.height();
    }

    // Start with a (very) rough estimation of the size
    int Width = 128, Height = 128;

    while (Width < MinWidth)
        Width *= 2;
    while (Height < MinHeight)
        Height *= 2;

    // Progressively scale up until we can pack into the image
    while (Width * Height < PixelCount || !PackInto(List, Width, Height))
    {
        if (Width <= Height)
            Width *= 2;
        else
            Height *= 2;
    }

    replay::pixbuf Result(Width, Height, replay::pixbuf::color_format::rgba);
    Result.fill(0, 0, 0, 0);

    BlitImages(Result, List);

    replay::pixbuf_io::save_to_png_file(File, Result);
}

void WriteBox(std::ostream& File, replay::box<int> const& Box)
{
    File << "x=" << Box.left << ", y=" << Box.bottom << ", w=" << Box.get_width() << ", h=" << Box.get_height();
}

void WriteTable(std::ofstream& File,
                std::string const& TableName,
                std::vector<std::pair<std::string, std::string>> KeysAndValues)
{
    using boost::algorithm::ilexicographical_compare;

    // Lua tables do not care about the order, so we can
    // sort them which makes for nicer diffs in the generated files
    auto CompareFirst = [](auto const& Left, auto const& Right) {
        return ilexicographical_compare(Left.first, Right.first);
    };

    std::sort(KeysAndValues.begin(), KeysAndValues.end(), CompareFirst);

    // Write out the actual table
    File << TableName << "={\n";
    for (auto i = KeysAndValues.begin(); i != KeysAndValues.end(); ++i)
    {
        File << fmt::format("  [\"{0}\"]={1}", i->first, i->second);

        if (i + 1 != KeysAndValues.end())
            File << ",\n";
        else
            File << "\n";
    }
    File << "}\n\n";
}

void WriteDictionary(std::ofstream& File,
                     std::string const& ModuleName,
                     std::string const& ImageTableName,
                     std::string const& NinePatchTableName,
                     std::vector<ImageEntryType> const& List)
{

    KeyValueList Sections;
    KeyValueList NinePatches;

    for (auto i = List.begin(); i != List.end(); ++i)
    {
        std::ostringstream Str;        
        auto&& Box = i->Box;

        if (i->IsNinePatch)
        {
            // Remove the file extension
            std::filesystem::path NameOnly = i->RelativePath;
            NameOnly.replace_extension();
            NameOnly.replace_extension(); // twice for the ".9"

            Str << "{Box={";
            WriteBox(Str, Box);
            Str << "}, Scalable={";
            WriteBox(Str, i->ScaleableArea);
            Str << "}, Fill={";
            WriteBox(Str, i->FillArea);
            Str << "}}";

            NinePatches.emplace_back(NameOnly.string(), Str.str());
        }
        else
        {
            // Remove the file extension
            auto NameOnly = i->RelativePath;
            NameOnly.replace_extension();

            Str << "{Box={";
            WriteBox(Str, Box);
            Str << "}}";

            Sections.emplace_back(NameOnly.string(), Str.str());
        }
    }

    File << "local " << ModuleName << "={}\n\n";

    if (!Sections.empty())
    {
        File << "-- Table for regular images\n";
        WriteTable(File, ModuleName + "." + ImageTableName, std::move(Sections));
    }

    if (!NinePatches.empty())
    {
        File << "-- Table for 9patch images\n";
        WriteTable(File, ModuleName + "." + NinePatchTableName, std::move(NinePatches));
    }

    File << "return " << ModuleName << std::endl;
}

void MakePackedImage(std::filesystem::path const& ImagePath,
                     std::filesystem::path const& ScriptPath,
                     std::string const& DictionaryName,
                     std::vector<std::string> const& SourceList)
{

    std::vector<ImageEntryType> ImageList;

    std::ofstream DictionaryFile(ScriptPath, std::ios::trunc);

    for (auto i = begin(SourceList); i != end(SourceList); ++i)
    {
        ScanFile(ImageList, *i);
    }

    // Construct the module name from the filename
    std::string ModuleName;
    {
        auto p = ScriptPath.filename();
        p.replace_extension();
        ModuleName = p.string();
    }

    // Sort according to size (max edge length)
    std::sort(ImageList.begin(), ImageList.end(), [](ImageEntryType const& Lhs, ImageEntryType const& Rhs) -> bool {
        auto const& a = Lhs.Image;
        auto const& b = Rhs.Image;
        return std::max(a.width(), a.height()) > std::max(b.width(), b.height());
    });

    PackImages(ImagePath, ImageList);
    WriteDictionary(DictionaryFile, ModuleName, DictionaryName, "NinePatches", ImageList);
}

int main(int argc, char* argv[])
{
    namespace po = boost::program_options;
    po::options_description desc("Allowed options");

    std::string ImagePath, ScriptPath, DictionaryName;
    std::string Code;
    std::vector<std::string> SourceList;

    desc.add_options()("image-path", po::value<std::string>(&ImagePath)->default_value("packed_image.png"),
                       "Set the path where to write the packed image data")(
        "script-path", po::value<std::string>(&ScriptPath)->default_value("packed_image.lua"),
        "Set the path where to write the dictionary for the image data")(
        "dict-name", po::value<std::string>(&DictionaryName)->default_value("Images"),
        "Set the variable-name for the generated table")("image", po::value<std::vector<std::string>>(&SourceList),
                                                         "Individual paths or folders to use as sources");

    po::positional_options_description p;
    p.add("image", -1);

    po::variables_map VariableMap;
    po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), VariableMap);

    po::notify(VariableMap);

    try
    {
        MakePackedImage(ImagePath, ScriptPath, DictionaryName, SourceList);
    }
    catch (std::exception const& Error)
    {
        std::cerr << Error.what();
    }

    return 0;
}
