#include <retesteth/configs/ClientConfig.h>
#include <retesteth/configs/Genesis.h>
#include <retesteth/ethObjects/object.h>
#include <mutex>
std::mutex g_staticDeclaration_clientConfigID;
namespace test
{
ClientConfigID::ClientConfigID()
{
    std::lock_guard<std::mutex> lock(g_staticDeclaration_clientConfigID);
    static unsigned uniqueID = 0;
    uniqueID++;
    m_id = uniqueID;
}

ClientConfig::ClientConfig(fs::path const& _clientConfigPath) : m_id(ClientConfigID())
{
    try
    {
        TestOutputHelper::get().setCurrentTestInfo(TestInfo("Client Config init"));
        fs::path configFile = _clientConfigPath / "config";
        ETH_FAIL_REQUIRE_MESSAGE(fs::exists(configFile), string("Client config not found: ") + configFile.c_str());

        // Load client config file
        m_clientConfigFile = GCP_SPointer<ClientConfigFile>(new ClientConfigFile(configFile));

        // Load genesis templates from default dir if not set in this folder
        fs::path genesisTemplatePath = _clientConfigPath / "genesis";
        if (!fs::exists(genesisTemplatePath))
        {
            genesisTemplatePath = _clientConfigPath.parent_path() / "default" / "genesis";
            ETH_FAIL_REQUIRE_MESSAGE(fs::exists(genesisTemplatePath), "default/genesis client config not found!");
        }

        // Load genesis templates
        for (auto const& net : cfgFile().allowedForks())
        {
            fs::path configGenesisTemplatePath = genesisTemplatePath / (net.asString() + ".json");
            ETH_FAIL_REQUIRE_MESSAGE(fs::exists(configGenesisTemplatePath),
                "\ntemplate '" + net.asString() + ".json' for client '" +
                    _clientConfigPath.stem().string() + "' not found ('" +
                    configGenesisTemplatePath.c_str() + "') in configs!");
            m_genesisTemplate[net] = test::readJsonData(configGenesisTemplatePath);
        }

        // Load correctmining Reward
        fs::path correctMiningRewardPath = genesisTemplatePath / "correctMiningReward.json";
        ETH_FAIL_REQUIRE_MESSAGE(fs::exists(correctMiningRewardPath),
            "correctMiningReward.json client config not found!");
        DataObject correctMiningReward = test::readJsonData(correctMiningRewardPath);
        correctMiningReward.performModifier(mod_removeComments);
        correctMiningReward.performModifier(mod_valueToCompactEvenHexPrefixed);
        for (auto const& el : cfgFile().forks())
        {
            if (!correctMiningReward.count(el.asString()))
                ETH_FAIL_MESSAGE("Correct mining reward missing block reward record for fork: `" +
                                 el.asString() + "` (" + correctMiningRewardPath.string() + ")");
            m_correctReward[el] = spVALUE(new VALUE(correctMiningReward.atKey(el.asString())));
        }
    }
    catch (std::exception const& _ex)
    {
        ETH_STDERROR_MESSAGE(string("Error initializing configs: ") + _ex.what());
    }
}

// Get shell script that runs an instance of a client from options folder
fs::path const ClientConfig::getShellPath() const
{
    fs::path parentFromConfigFile = cfgFile().path().parent_path();
    fs::path shellScript = parentFromConfigFile.stem().string() + ".sh";
    ETH_FAIL_REQUIRE_MESSAGE(fs::exists(shellScript), "Shell script does not exist: " + shellScript.string());
    return shellScript;
}

void ClientConfig::checkForkAllowed(FORK const& _net) const
{
    if (!cfgFile().allowedForks().count(_net))
    {
        ETH_WARNING("Specified network not found: '" + _net.asString() +
                    "', skipping the test. Enable the fork network in config file: " +
                    cfgFile().path().string());
        ETH_ERROR_MESSAGE("Specified network not found: '" + _net.asString() + "'");
    }
}

/// translate network names in expect section field
/// >Homestead to EIP150, EIP158, Byzantium, ...
/// <=Homestead to Frontier, Homestead
set<FORK> ClientConfig::translateNetworks(set<string> const& _networks) const
{
    // Construct vector with test network names in a right order
    // (from Frontier to Homestead ... to Constantinople)
    // According to fork order in config file
    set<FORK> out;
    for (auto const& net : _networks)
    {
        std::vector<FORK> const& forkOrder = cfgFile().forks();

        bool isNetworkTranslated = false;
        string possibleNet = net.substr(1, net.length() - 1);
        vector<FORK>::const_iterator it =
            std::find(forkOrder.begin(), forkOrder.end(), possibleNet);

        if (it != forkOrder.end() && net.size() > 1)
        {
            if (net[0] == '>')
            {
                while (++it != forkOrder.end())
                {
                    out.emplace(*it);
                    isNetworkTranslated = true;
                }
            }
            else if (net[0] == '<')
            {
                while (it != forkOrder.begin())
                {
                    out.emplace(*(--it));
                    isNetworkTranslated = true;
                }
            }
        }

        possibleNet = net.substr(2, net.length() - 2);
        it = std::find(forkOrder.begin(), forkOrder.end(), possibleNet);
        if (it != forkOrder.end() && net.size() > 2)
        {
            if (net[0] == '>' && net[1] == '=')
            {
                while (it != forkOrder.end())
                {
                    out.emplace(*(it++));
                    isNetworkTranslated = true;
                }
            }
            else if (net[0] == '<' && net[1] == '=')
            {
                out.emplace(*it);
                isNetworkTranslated = true;
                while (it != forkOrder.begin())
                    out.emplace(*(--it));
            }
        }

        // if nothing has been inserted, just push the untranslated network as is
        if (!isNetworkTranslated)
        {
            checkForkAllowed(FORK(net));
            out.emplace(FORK(net));
        }
    }
    return out;
}

std::string const& ClientConfig::translateException(string const& _exceptionName) const
{
    ClientConfigFile const& cfg = *m_clientConfigFile.getCPtr();
    if (cfg.exceptions().count(_exceptionName))
        return cfg.exceptions().at(_exceptionName);

    // --- Correct typos
    // Puth known exceptions into vector
    vector<string> exceptions;
    for (auto const& el : cfg.exceptions())
        exceptions.push_back(el.first);

    auto const suggestions = test::levenshteinDistance(_exceptionName, exceptions, 5);
    string message = " (Suggestions: ";
    for (auto const& el : suggestions)
        message += el + ", ";
    message += " ...)";
    ETH_ERROR_MESSAGE("Config::getExceptionString '" + _exceptionName +
                      "' not found in client config `exceptions` section! (" + cfg.path().c_str() +
                      ")" + message);
    // ---
    static string const notfound = string();
    return notfound;
}


}  // namespace test
