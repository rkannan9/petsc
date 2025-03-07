#!/usr/bin/env python3
import numpy as np
import os
import sys
import importlib
import datetime as date

# Check to ensure that the environmental variable PETSC_DIR has been assigned.
# MPLCONFIGDIR is needed for matplotlib
try:
    if os.environ.get('PETSC_DIR') is None:
        raise NotADirectoryError()
    os.environ['MPLCONFIGDIR'] = os.environ.get(
        'PETSC_DIR')+'/share/petsc/xml/'
except NotADirectoryError:
    sys.exit('The environmental variable PETSC_DIR was not found.\n'
             'Please add this variable with the base directory for PETSc or the base directory that MPLCONFIGDIR resides')

import matplotlib.pyplot as plt
import argparse
import math
import configureTAS as config
import pandas as pd
from tasClasses import File
from tasClasses import Field

def main(cmdLineArgs):
    data = []
    # This section handles the command arguments that edit configurTas.py
    if cmdLineArgs.setDefaultGraphDir is not None:
        aliasInConfig, editConfig = checkAlias(
            'defaultGraphs', cmdLineArgs.setDefaultGraphDir[0])

        if editConfig and checkDirforFilePath('defaultGraphs', cmdLineArgs.setDefaultGraphDir[0]):
            result = editConfigureTasFile(
                'defaultGraphs', 'add', aliasInConfig, cmdLineArgs.setDefaultGraphDir[0])

        if result:
            print(f'\nconfigureTAS.py defaultGraphs was updated with path {cmdLineArgs.setDefaultGraphDir[0]}\n')

        exit()

    if cmdLineArgs.setDefaultFileDir is not None:
        aliasInConfig, editConfig = checkAlias(
            'defaultData', cmdLineArgs.setDefaultFileDir[0])

        if editConfig and checkDirforFilePath('defaultData', cmdLineArgs.setDefaultFileDir[0]):
            result = editConfigureTasFile(
                'defaultData', 'add', aliasInConfig, cmdLineArgs.setDefaultFileDir[0])

        if result:
            print(f'\nconfigureTAS.py defaultData was updated with path {cmdLineArgs.setDefaultFileDir[0]}\n')

        exit()

    if cmdLineArgs.addEditAliasDir is not None:
        listToAdd = cmdLineArgs.addEditAliasDir
        counter = 0
        if len(listToAdd) % 2 == 0:
            for counter in range(0, len(listToAdd)-1, 2):
                aliasInConfig, editConfig = checkAlias(
                    listToAdd[counter], listToAdd[counter+1])

                if editConfig and checkDirforFilePath(listToAdd[counter], listToAdd[counter+1]):
                    result = editConfigureTasFile(
                        listToAdd[counter], 'add', aliasInConfig, listToAdd[counter+1])
                    if result:
                        print(f'\nconfigureTAS.py was updated with\n\talias: {listToAdd[counter]}\n\tpath {listToAdd[counter+1]}\n')
                    else:
                        print(f'\nconfigureTAS.py was NOT updated with \n\talias: {listToAdd[counter]}\n\tpath {listToAdd[counter+1]}\n')
        else:
            print(f'\nWhen using the command line option to add or edit an alias path pair, both must be included.\n'
                  f'Your input was:')
            for item in cmdLineArgs.addEditAliasDir:
                print(item)
        exit()

    if cmdLineArgs.removeAliasDir is not None:
        for alias in cmdLineArgs.removeAliasDir:
            if alias in config.filePath:
                result = editConfigureTasFile(alias, 'remove')
                if result:
                    print(f'\n{alias} was successfully removed from configureTAS.py\n')
                else:
                    print(f'\n{alias} was not successful removed from configureTAS.py\n')
            else:
                print(f'\n{alias} was not found in configureTAS.py\nList of valid aliases is:\n')
                for alias in config.filePath:
                    print(alias)
        exit()

    if cmdLineArgs.file is None:
        if cmdLineArgs.pathAliasData is None:
            files = getFiles(cmdLineArgs, 'defaultData')
        else:
            files = getFiles(cmdLineArgs, cmdLineArgs.pathAliasData[0])
    else:
        files = getFiles(cmdLineArgs, None)

    if len(files['module']) != 0:
        for fileName in files['module']:
            data.append(dataProces(cmdLineArgs, fileName))

    if len(files['csv']) != 0:
        for fileName in files['csv']:
            data.append(dataProcesCSV(cmdLineArgs, fileName))

    for item in data:
        graphGen(item, cmdLineArgs.enable_graphs,
                 cmdLineArgs.graph_flops_scaling, cmdLineArgs.dim)

    exit()


def checkAlias(alias, path):
    """
    This function checks to see if the alias to be added/updated, entered on the command line, already exits
    in configureTAS.py.  In configureTAS.py the alias is the key in the Dictionary filePath.  If that key exists
    The current key value pair is displayed and the user is prompted to confirm replacement.  The function
    returns True or False based on the user response.

    If the key is not present the function returns True, without prompting.

    :param alias: Contains the string to compare to the keys in Dictionary filePath.
    :param path: Contains the new path as entered on the command line.

    :returns:   a tuple of True/False, The first is True if the alias exists in configureTAS.py and the second is
                True if is should continue and edit configureTAS.py..
    """
    
    if alias in config.filePath:
        print(f'\nalias: {alias}\nalready has path: {config.filePath[alias]}\n\n'
              f'Do you wish to replace with \npath: {path}\n')

        response = input('(y/n) to continue\n')

        if response.lower() == 'y':
            return True, True
        else:
            return True, False

    else:
        return False, True


def checkDirforFilePath(alias, path):
    """
    This function checks to see if the path to be added/updated, as entered on the command line, already exits
    in the file system. If the path exists the function returns True.

    If it does not the user is prompted with the option of creating the directory.  If the user responds
    yes an attempt will be made to create the directory.  If it is successful a message that it was created is
    displayed and the function returns True.

    If there is an error then a message is displayed and the user is asked if theyh wish to continue.  If they
    respond yes the function will return True, otherwise it will return False.

    If during any of the other prompts the user responds with no, the function returns False.

    :param alias: Contains the string to compare to the keys in Dictionary filePath.
    :param path: Contains the new path as entered on the command line.

    :returns:   True/False, True if the program should edit configureTAS.py and False to not edit it.
    """

    if os.path.isdir(path):
        return True
    else:
        print(f'\nDirectory: {path}\nfor alias: {alias}\nDoes not exist.\n'
              f'Do you wish to create the directory\n')
        response = input('(y/n) to continue\n')
        if response.lower() == 'y':
            try:
                os.mkdir(path)
                print('Directory successfully created.\n')
                return True
            except os.error:
                print(f'\nAn error occurred while attempting to create directory:\n{path}\n\n'
                      f'Please check to make sure that you have permission to create directories\n\n'
                      f'Do you with to continue with adding {path} to configureTAS.py?')
                response = input('(y/n) to continue\n')
                if response.lower() == 'y':
                    return True
                else:
                    return False
        else:
            print('\nDo you wish to continue adding the alias path pair to configureTAS.py?\n')
            response = input('(y/n) to continue\n')
            if response.lower() == 'y':
                return True
            else:
                return False


def editConfigureTasFile(alias, mode, aliasInConfig=False, path=None):
    """
    This function edits configureTAS.py by updating an alias path pair, adding a new one, or removing one.

    :param alias: Contains the string to compare to the keys in Dictionary filePath.
    :param path: Contains the new path as entered on the command line.
    :param mode: Contains a string, add or remove.  If it is add then an alias will be added
                    or edited.  If it is remove an alias and path will be removed.

    :returns:   True/False,True if the file is updated, False if not.
    """

    linesToWrite = []
    with open('configureTAS.py', 'r') as configureTASFile:
        for line in configureTASFile:
            if mode == 'add':
                if aliasInConfig:
                    if alias in line:
                        linesToWrite.append(
                            'filePath[\'' + alias + '\']=' + '\'' + path + '\'\n')
                    else:
                        linesToWrite.append(line)
                else:
                    if 'defaultData' in line:
                        linesToWrite.append(line)
                        linesToWrite.append(
                            'filePath[\'' + alias + '\']=' + '\'' + path + '\'\n')
                    else:
                        linesToWrite.append(line)
            else:
                if alias in line:
                    continue
                else:
                    linesToWrite.append(line)

    with open('configureTAS.py', 'w') as configureTASFile:
        configureTASFile.writelines(linesToWrite)

    # Code to make sure configureTAS was updated.
    importlib.reload(config)

    if mode == 'add':
        if alias in config.filePath:
            return True
        else:
            return False
    else:
        if alias not in config.filePath:
            return True
        else:
            return False


def getFiles(cmdLineArgs, alias):
    """
    This function first determines if it should look in the pathway specified in filePath['defaultData']
    in the configurationTAS.py file or a file name given as a command line argument using -f or -file.
    It then builds lists of file names and stores them in a dictionary, where they keys correspond to
    the type of file, ie, module(ASCII type) or CSV.

    :param cmdLineArgs: Contains command line arguments.

    :returns:   files, a dictionary with keys whose values are lists of file names, grouped by type
                of file.
    """

    files = {'module': [], 'csv': []}

    if alias is not None:
        try:
            if not config.filePath[alias]:
                raise NotADirectoryError()
            dataPath = config.filePath[alias]
            filesTemp = os.listdir(dataPath)
            for f in filesTemp:
                if f[-3:] == '.py':
                    files['module'].append(f[0:len(f)-3])
                elif f[-4:] == '.pyc':
                    files['module'].append(f[0:len(f)-4])
                elif f[-4:] == '.csv':
                    files['csv'].append(f)
            if len(filesTemp) == 0 or len(files) == 0:
                raise IOError()
        except NotADirectoryError:
            print(f'The path for {alias} in configureTAS.py is empty and no valid file was specified using the -file/-f argument. \n'
                  f'Please either specify a path in configureTAS.py or use the command line argument -file/-f')
        except IOError:
            sys.exit('No valid data files in ' + dataPath + ' and -file/-f argument is empty. \n'
                     'Please check for .py, .pyc, or .csv files in '
                     + dataPath + ' or specify one with the -file/-f '
                     'argument.')
    else:
        for file in cmdLineArgs.file:
            if not os.path.exists(file):
                print(f'{file} is not a valid path or file name')
            else:
                if file[-4:] == '.csv':
                    print('csv file')
                    files['csv'].append(file)
                else:
                    files['module'].append(file)
    for key in files.keys():
        print(f'key: {key}, items: {files[key]}')
    return files


def dataProcesCSV(cmdLineArgs, fileName):
    """
    This function takes the list of data files in CSV format supplied as a list and parses them into a tasClasses
    object, whose top level key is the file name, followed by data type, i.e. dofs, times, flops, errors, and
    the finale value is a NumPy array of the data to plot.

    :param cmdLineArgs: Contains command line arguments.
    :param fileNames: Contains the CSV file names.
    :type string:

    :returns:   data a tasClasses file object containing the parsed data from the files specified on the command line.
    """
    data = {}
    results = []

    if(cmdLineArgs.file == None):
        if cmdLineArgs.pathAliasData == None:
            os.chdir(config.filePath['defaultData'])
        else:
            os.chdir(config.filePath[cmdLineArgs.pathAliasData[0]])

    if('/' in fileName):
        path_fileName = os.path.split(fileName)
        os.chdir(path_fileName[0])
        fileName = path_fileName[1]

    df = pd.read_csv(fileName)
    Nf = getNfCSV(df)
    nProcs = int(df.columns.tolist()[25])
    dofs = []
    errors = []

    times = []
    timesMin = []
    meanTime = []
    timeGrowthRate = []

    flops = []
    flopsMax = []
    flopsMin = []
    meanFlop = []
    flopGrowthRate = []

    luFactor = []
    luFactorMin = []
    luFactorMean = []
    luFactorGrowthRate = []

    file = File(fileName[0:len(fileName)-4])

    # filters for using in df.loc[]

    # Needed for SNES problems
    SNESSolveFilter = (df['Event Name'] == 'SNESSolve')
    # Needed for Time Step problems
    TSStepFilter = (df['Event Name'] == 'TSStep')
    MatLUFactorFilter = ((df['Event Name'] == 'MatLUFactorNum')
                         | (df['Event Name'] == 'MatLUFactorSym'))
    ConvEstErrorFilter = (df['Event Name'] == 'ConvEst Error')
    rankFilter = (df['Rank'] == 0)

    if cmdLineArgs.timestep == 0:
        SolverFilter = SNESSolveFilter
        # Added a check to make sure the problem is truly a SNES rather than TS
        if (df.loc[SNESSolveFilter & (df['Stage Name'] == 'ConvEst Refinement Level 0') & rankFilter, 'Time'] == 0).bool():
            print(f'The sampled time value for SNESSolve is 0.  This most commonly happens if the problem'
                  f' is a Time Step problem.\n If this is a Time Step problem hit (y) to apply the the Time Step filter'
                  f'(This can also be done on the command line using -ts 1).\nOtherwise hit (n) to continue using the SNES Solver filter')
            response = input()
            if response.lower() == 'y':
                SolverFilter = TSStepFilter
    else:
        SolverFilter = TSStepFilter

    for f in range(Nf):
        errors.append([])
    for f in range(Nf):
        dofs.append([])

    level = 0
    while level >= 0:
        if ('ConvEst Refinement Level ' + str(level) in df['Stage Name'].values):
            stageName = 'ConvEst Refinement Level '+str(level)
            #Level dependent filters
            stageNameFilter = (df['Stage Name'] == stageName)
            fieldFilter = stageNameFilter & ConvEstErrorFilter & rankFilter

            SolverDf = df.loc[stageNameFilter & SolverFilter]

            MatLUFactorDf = df.loc[(stageNameFilter & MatLUFactorFilter), [
                                    'Time', 'Rank']]
            # groupby done in order to get the sum of MatLUFactorNum and MatLUFactorSym
            # For each Rank/CPU
            MatLUFactorDf = MatLUFactorDf.groupby(['Rank']).sum()

            meanTime.append((SolverDf['Time'].sum())/nProcs)
            times.append(SolverDf['Time'].max())
            timesMin.append(SolverDf['Time'].min())

            meanFlop.append((SolverDf['FLOP'].sum())/nProcs)
            flops.append(SolverDf['FLOP'].sum())
            flopsMax.append(SolverDf['FLOP'].max())
            flopsMin.append(SolverDf['FLOP'].min())

            if level >= 1:
                timeGrowthRate.append(meanTime[level]/meanTime[level-1])
                flopGrowthRate.append(meanFlop[level]/meanFlop[level-1])

            luFactorMean.append(MatLUFactorDf.sum()/nProcs)
            luFactor.append(MatLUFactorDf.max())
            luFactorMin.append(MatLUFactorDf.min())

            for f in range(Nf):
                dofs[f].append((df.loc[fieldFilter])['dof'+str(f)].values[0])
                errors[f].append((df.loc[fieldFilter])['e'+str(f)].values[0])

            level = level + 1
        else:
            level = -1

    dofs = np.array(dofs, dtype=object)
    errors = np.array(errors, dtype=object)

    times = np.array(times)
    meanTime = np.array(meanTime)
    timesMin = np.array(timesMin)
    timeGrowthRate = np.array(timeGrowthRate)

    flops = np.array(flops)
    meanFlop = np.array(meanFlop)
    flopsMax = np.array(flopsMax)
    flopsMin = np.array(flopsMin)
    flopGrowthRate = np.array(flopGrowthRate)

    luFactor = np.array(luFactor)
    luFactorMin = np.array(luFactorMin)
    luFactorMean = np.array(luFactorMean)
    luFactorGrowthRate = np.array(luFactorGrowthRate)

    data['Times'] = times
    data['Mean Time'] = meanTime
    data['Times Range'] = times-timesMin
    data['Time Growth Rate'] = timeGrowthRate

    data['Flops'] = flops
    data['Mean Flops'] = meanFlop
    data['Flop Range'] = flopsMax - flopsMin
    data['Flop Growth Rate'] = flopGrowthRate

    data['LU Factor'] = luFactor
    data['LU Factor Mean'] = luFactorMean
    data['LU Factor Range'] = luFactor-luFactorMin
    data['LU Factor Growth Rate'] = luFactorGrowthRate

    for f in range(Nf):
        try:
            if cmdLineArgs.fieldList is not None:
                if len(cmdLineArgs.fieldList) != Nf:
                    print(f'\nYou specified {len(cmdLineArgs.fieldList)} from the command line, while the log file has {Nf} fields.\n\n'
                          f'The fields you specified were:\n{cmdLineArgs.fieldList}\n\n')

                    response = input('(y/n) to continue without field names\n')

                    if response.lower() == 'n':
                        exit()
                    else:
                        cmdLineArgs.fieldList = None
                        file.addField(Field(file.fileName, str(f)))
                else:
                    file.addField(
                        Field(file.fileName, cmdLineArgs.fieldList[f]))
            elif cmdLineArgs.problem != 'NULL':
                file.addField(
                    Field(file.fileName, config.fieldNames[cmdLineArgs.problem]['field '+str(f)]))
            else:
                file.addField(Field(file.fileName, str(f)))
        except KeyError:
            sys.exit('The problem you specified on the command line: ' + cmdLineArgs.problem + ' \ncould not be found'
                     ' please check ' + config.__file__ + ' to ensure that you are using the correct name/have defined the fields for the problem.')

    file.fileData = data
    for f in range(Nf):
        print(f)
        file.fieldList[f].fieldData['dofs'] = dofs[f]
        file.fieldList[f].fieldData['Errors'] = errors[f]

    file.printFile()

    return file


def dataProces(cmdLineArgs, fileName):
    """
    This function takes a data file, ASCII type, for supplied as command line arguments and parses it into a multi-level
    dictionary, whose top level key is the file name, followed by data type, i.e. dofs, times, flops, errors, and
    the finale value is a NumPy array of the data to plot.  This is the used to generate a tasClasses File object

        data[<file name>][<data type>]:<numpy array>

    :param cmdLineArgs: Contains the command line arguments.
    :param fileName: Contains the name of file to be processed
    :type string:

    :returns:   data a tasClasses File object containing the parsed data from the file specified on the command line.
    """

    data = {}
    files = []
    results = []
    #if -file/-f was left blank then this will automatically add every .py and .pyc
    #file to the files[] list to be processed.

    module = importlib.import_module(fileName)
    Nf = getNf(module.Stages['ConvEst Refinement Level 1']
               ['ConvEst Error'][0]['error'])
    nProcs = module.size
    dofs = []
    errors = []

    times = []
    timesMin = []
    meanTime = []
    timeGrowthRate = []

    flops = []
    flopsMax = []
    flopsMin = []
    meanFlop = []
    flopGrowthRate = []

    luFactor = []
    luFactorMin = []
    luFactorMean = []
    luFactorGrowthRate = []

    file = File(module.__name__)

    for f in range(Nf):
        try:
            if cmdLineArgs.problem != 'NULL':
                file.addField(
                    Field(file.fileName, config.fieldNames[cmdLineArgs.problem]['field '+str(f)]))
            else:
                file.addField(Field(file.fileName, str(f)))
        except:
            sys.exit('The problem you specified on the command line: ' + cmdLineArgs.problem + ' \ncould not be found'
                     ' please check ' + config.__file__ + ' to ensure that you are using the correct name/have defined the fields for the problem.')

    for f in range(Nf):
        errors.append([])
    for f in range(Nf):
        dofs.append([])

    level = 0
    while level >= 0:
        stageName = 'ConvEst Refinement Level '+str(level)
        if stageName in module.Stages:
            timeTempMax = module.Stages[stageName]['SNESSolve'][0]['time']
            timeTempMin = module.Stages[stageName]['SNESSolve'][0]['time']
            totalTime = module.Stages[stageName]['SNESSolve'][0]['time']

            flopsTempMax = module.Stages[stageName]['SNESSolve'][0]['flop']
            flopsTempMin = module.Stages[stageName]['SNESSolve'][0]['flop']
            totalFlop = module.Stages[stageName]['SNESSolve'][0]['flop']

            luFactorTempMax = module.Stages[stageName]['MatLUFactorNum'][0]['time'] + \
                module.Stages[stageName]['MatLUFactorSym'][0]['time']
            luFactorTempMin = luFactorTempMax
            totalLuFactor = luFactorTempMax

            #This loops is used to grab the greatest time and flop when run in parallel
            for n in range(1, nProcs):
                #Sum of MatLUFactorNum and MatLUFactorSym
                if module.Stages[stageName]['MatLUFactorNum'][n]['time'] != 0:
                    luFactorCur = module.Stages[stageName]['MatLUFactorNum'][n]['time'] + \
                        module.Stages[stageName]['MatLUFactorSym'][n]['time']

                #Gather Time information
                timeTempMax = timeTempMax if timeTempMax >= module.Stages[stageName]['SNESSolve'][n]['time'] \
                    else module.Stages[stageName]['SNESSolve'][n]['time']
                timeTempMin = timeTempMin if timeTempMin <= module.Stages[stageName]['SNESSolve'][n]['time'] \
                    else module.Stages[stageName]['SNESSolve'][n]['time']
                totalTime = totalTime + \
                    module.Stages[stageName]['SNESSolve'][n]['time']

                #Gather Flop information
                flopsTempMax = flopsTempMax if flopsTempMax >= module.Stages[stageName]['SNESSolve'][n]['flop'] \
                    else module.Stages[stageName]['SNESSolve'][n]['flop']
                flopsTempMin = flopsTempMin if flopsTempMin <= module.Stages[stageName]['SNESSolve'][n]['flop'] \
                    else module.Stages[stageName]['SNESSolve'][n]['flop']
                totalFlop = totalFlop + \
                    module.Stages[stageName]['SNESSolve'][n]['flop']

                if module.Stages[stageName]['MatLUFactorNum'][n]['time'] != 0:
                    luFactorTempMax = luFactorTempMax if luFactorTempMax >= luFactorCur \
                            else luFactorCur
                    luFactorTempMin = luFactorTempMin if luFactorTempMin <= luFactorCur \
                        else luFactorCur
                    totalLuFactor = totalLuFactor + luFactorCur

            meanTime.append(totalTime/nProcs)
            times.append(timeTempMax)
            timesMin.append(timeTempMin)

            meanFlop.append(totalFlop/nProcs)
            flops.append(totalFlop)
            flopsMax.append(flopsTempMax)
            flopsMin.append(timeTempMin)
            if module.Stages[stageName]['MatLUFactorNum'][n]['time'] != 0:
                luFactor.append(luFactorTempMax)
                luFactorMin.append(luFactorTempMin)
                luFactorMean.append(totalLuFactor/nProcs)

            #Calculates the growth rate of statistics between levels
            if level >= 1:
                timeGrowthRate.append(meanTime[level]/meanTime[level-1])
                flopGrowthRate.append(meanFlop[level]/meanFlop[level-1])

            #TODO FOR SNES
            #if module.Stages[stageName]['MatLUFactorNum'][n]['time'] != 0:
            #    luFactorGrowthRate.append(luFactorMean[level-1]/luFactorMean[level-2])

            for f in range(Nf):
                dofs[f].append(module.Stages[stageName]
                               ['ConvEst Error'][0]['dof'][f])
                errors[f].append(module.Stages[stageName]
                                 ['ConvEst Error'][0]['error'][f])

            level = level + 1
        else:
            level = -1

    dofs = np.array(dofs)
    errors = np.array(errors)

    times = np.array(times)
    meanTime = np.array(meanTime)
    timesMin = np.array(timesMin)
    timeGrowthRate = np.array(timeGrowthRate)

    flops = np.array(flops)
    meanFlop = np.array(meanFlop)
    flopsMax = np.array(flopsMax)
    flopsMin = np.array(flopsMin)
    flopGrowthRate = np.array(flopGrowthRate)

    luFactor = np.array(luFactor)
    luFactorMin = np.array(luFactorMin)
    luFactorMean = np.array(luFactorMean)
    luFactorGrowthRate = np.array(luFactorGrowthRate)

    data['Times'] = times
    data['Mean Time'] = meanTime
    data['Times Range'] = times-timesMin
    data['Time Growth Rate'] = timeGrowthRate

    data['Flops'] = flops
    data['Mean Flops'] = meanFlop
    data['Flop Range'] = flopsMax - flopsMin
    data['Flop Growth Rate'] = flopGrowthRate

    data['LU Factor'] = luFactor
    data['LU Factor Mean'] = luFactorMean
    data['LU Factor Range'] = luFactor-luFactorMin
    data['LU Factor Growth Rate'] = luFactorGrowthRate

    file.fileData = data
    for f in range(Nf):
        file.fieldList[f].fieldData['dofs'] = dofs[f]
        file.fieldList[f].fieldData['Errors'] = errors[f]

    file.printFile()

    return file


def getNf(errorList):
    """
    This simple function takes the supplied error list and loops through that list until it encounters -1.  The default
    convention is that each field from the problem has an entry in the error list with at most 8 fields.  If there are
    less than 8 fields those entries are set to -1.
    Example:
      A problem with 4 fields would have a list of the form [.01, .003, .2, .04, -1, -1, -1, -1]

    :param errorList: contains a list of floating point numbers with the errors from each level of refinement.
    :type errorList: List containing Floating point numbers.
    :returns: Nf an integer that represents the number of fields.
    """
    i = 0
    Nf = 0
    while errorList[i] != -1:
        Nf = Nf + 1
        i += 1
    return Nf


def getNfCSV(df):
    """
    This simple function is the same as getNf, except it is for the CSV files. It loops through
    the values of the dofx columns, where x is an integer, from the row where
    Stage Name = ConvEst Refinement Level 0, Event Name = ConvEst Error, and Rank = 0 until it
    encounters -1.  The default convention is that each field from the problem has an entry in the error list with at most
    8 fields.  If there are less than 8 fields those entries are set to -1.

    Example:
      A problem with 4 fields would have a list of the form [.01, .003, .2, .04, -1, -1, -1, -1]

    :param df: Contains a Pandas Data Frame.
    :type df: A Pandas Data Frame object.
    :returns: Nf an integer that represents the number of fields.
    """
    #Get a single row from the Data Frame that contains the field information
    df = df.loc[(df['Event Name'] == 'ConvEst Error') & (df['Stage Name'] == 'ConvEst Refinement Level 0')
                & (df['Rank'] == 0)].reset_index()
    level = 1
    while level >= 1:
        dof = 'dof' + str(level)
        if df.loc[0, dof] == -1:
            break
        else:
            level = level + 1
    return level


def graphGen(file, enable_graphs, graph_flops_scaling, dim):
    """
    This function takes the supplied dictionary and plots the data from each file on the Mesh Convergence, Static Scaling, and
    Efficacy graphs.

    :param file: Contains the data to be plotted on the graphs, assumes the format -- file[<file name>][<data type>]:<numpy array>
    :type file: Dictionary
    :param graph_flops_scaling: Controls creating the scaling graph that uses flops/second.  The default is not to.  This option
                                    is specified on the command line.
    :type graph_flops_scaling: Integer
    :param dim: Contains the number of dimension of the mesh.  This is specified on the command line.
    :type dim: Integer


    :returns: None
    """
    lstSqMeshConv = np.empty([2])

    counter = 0
    #Loop through each file and add the data/line for that file to the Mesh Convergence, Static Scaling, and Efficacy Graphs
    for field in file.fieldList:
        #Least squares solution for Mesh Convergence
        if isinstance(field.fieldData['Errors'][0], str) or field.fieldData['Errors'][0] == -1:
            print('Mesh Convergence can not be calculated, nan values in Error field will change to 1')
            for x in range(len(field.fieldData['Errors'])):
                field.fieldData['Errors'][x] = 1



        lstSqMeshConv[0], lstSqMeshConv[1] = leastSquares(
            field.fieldData['dofs'], field.fieldData['Errors'])
        print('Least Squares Data')
        print('==================')
        print('Mesh Convergence')
        print('Alpha: {} \n  {}'.format(lstSqMeshConv[0], lstSqMeshConv[1]))

        convRate = lstSqMeshConv[0] * -dim
        print('convRate: {} of {} field'.format(convRate, field.fieldName))

        field.setConvergeRate(convRate)
        field.setAlpha(lstSqMeshConv[0])
        field.setBeta(lstSqMeshConv[1])

    if cmdLineArgs.enable_graphs == 1:
        #Uses the specified style sheet for generating the plots
        styleDir = os.path.join(os.environ.get('PETSC_DIR'), 'lib/petsc/bin')
        plt.style.use(os.path.join(styleDir, 'petsc_tas_style.mplstyle'))

        #Set up plots with labels
        if not pd.isna(field.fieldData['Errors'][0]):
            meshConvFig = plt.figure()
            meshConvOrigHandles = []
            meshConvLstSqHandles = []
            axMeshConv = meshConvFig.add_subplot(1, 1, 1)
            axMeshConv.set(xlabel='Problem Size $\log N$', ylabel='Error $\log |x - x^*|$', title='Mesh Convergence')

        statScaleFig = plt.figure()
        statScaleHandles = []
        axStatScale = statScaleFig.add_subplot(1, 1, 1)
        axStatScale.set(xlabel='Time(s)', ylabel='Flop Rate (F/s)', title='Static Scaling')

        statScaleFig = plt.figure()
        statScaleHandles = []
        axStatScale = statScaleFig.add_subplot(1, 1, 1)
        axStatScale.set(xlabel='Time(s)', ylabel='DoF Rate (DoF/s)', title='Static Scaling')
        
        efficFig = plt.figure()
        efficHandles = []
        axEffic = efficFig.add_subplot(1, 1, 1)
        axEffic.set(xlabel='Time(s)', ylabel='Error Time', title='Efficacy')
        axEffic.set_ylim(0, 10)

        #Loop through each file and add the data/line for that file to the Mesh Convergence, Static Scaling, and Efficacy Graphs
        for field in file.fieldList:
            ##Start Mesh Convergence graph
            convRate = str(round(field.cRate, 3))

            x, = axMeshConv.loglog(field.fieldData['dofs'], field.fieldData['Errors'],
                                   label='Field ' + field.fieldName + ' Orig Data', marker='^')

            meshConvOrigHandles.append(x)

            y, = axMeshConv.loglog(field.fieldData['dofs'], ((field.fieldData['dofs']**lstSqMeshConv[0] * 10**lstSqMeshConv[1])),
                                   label=field.fieldName + ' Convergence rate =  ' + convRate, marker='x')

            #meshConvLstSqHandles.append(y)

            ##Start Static Scaling Graph, only if graph_flops_scaling equals 1.  Specified on the command line.
            if graph_flops_scaling == 1:
                x, = axStatScale.loglog(file.fileData['Times'], file.fileData['Flops']/file.fileData['Times'],
                                        label='Field ' + field.fieldName, marker='^')

            ##Start Static Scaling with DoFs Graph
            x, = axStatScale.loglog(file.fileData['Times'], field.fieldData['dofs']/file.fileData['Times'],
                                    label='Field ' + field.fieldName, marker='^')

            statScaleHandles.append(x)
            ##Start Efficacy graph
            x, = axEffic.semilogx(file.fileData['Times'], -np.log10((field.fieldData['Errors']*file.fileData['Times']).astype(np.float64)),
                                  label='Field ' + field.fieldName, marker='^')

            efficHandles.append(x)

            counter = counter + 1

        #meshConvHandles = meshConvOrigHandles + meshConvLstSqHandles
        #meshConvLabels = [h.get_label() for h in meshConvOrigHandles]
        #meshConvLabels = meshConvLabels + [h.get_label() for h in meshConvLstSqHandles]

        #meshConvFig.legend(handles=meshConvHandles, labels=meshConvLabels)
        meshConvFig.legend()
        #statScaleLabels = [h.get_label() for h in statScaleHandles]
        #statScaleFig.legend(handles=statScaleHandles, labels=statScaleLabels)
        statScaleFig.legend()

        #efficLabels = [h.get_label() for h in efficHandles]
        #efficFig.legend(handles=efficHandles, labels=efficLabels)
        efficFig.legend()

        axStatScale.set_ylim(ymin=0.1)

        #code for determining if the default path for graphs, in configureTAS.py should be used or
        #if an alias was given for a different path on the command line.
        if cmdLineArgs.pathAliasGraph is None:
            if config.filePath['defaultGraphs'] is None:
                print(f'The defaultGraphs alias is not set.  \nPlease either specify an alias using the'
                      f' -pag command line option or set a defaultGraphs path using -dgd')
                exit()
            else:
                pathAlias = 'defaultGraphs'
        else:
            if cmdLineArgs.pathAliasGraph[0] in config.filePath:
                pathAlias = cmdLineArgs.pathAliasGraph[0]

            elif 'defaultGraphs' in config.filePath and config.filePath['defaultGraphs'] is not None:
                defGraphPath = config.filePath['defaultGraphs']
                print(f'\nAlias {cmdLineArgs.pathAliasGraph[0]} was not found in configureTAS.py\n'
                      f'Do you wish to use the default path of {defGraphPath}')

                response = input('y/n to continue')

                if response.lower() == 'y':
                    pathAlias = 'defaultGraphs'
                else:
                    exit()
            else:
                print(f'\nAlias {cmdLineArgs.pathAliasGraph[0]} was not found in configureTAS.py'
                      f'and defaultGraphs path is empty.\nPlease set these through the command line options:\n'
                      f'-dgd to set the default path for graphs or\n-aefd to set additional aliases\n')

        meshConvFig.savefig(
            config.filePath[pathAlias]+'meshConvergenceField_' + field.fileName + '.png')
        statScaleFig.savefig(
            config.filePath[pathAlias]+'staticScalingField_' + field.fileName + '.png')
        efficFig.savefig(
            config.filePath[pathAlias]+'efficacyField_' + field.fileName + '.png')

    return


def leastSquares(x, y):
    """
    This function takes 2 numpy arrays of data and out puts the least squares solution,
       y = m*x + c.  The solution is obtained by finding the result of y = Ap, where A
       is the matrix of the form [[x 1]] and p = [[m], [c]].

    :param x: Contains the x values for the data.
    :type x: numpy array
    :param y: Contains the y values for the data.
    :type y: numpy array

    :returns: alpha -- the convRate for the least squares solution
    :returns: c -- the constant of the least squares solution.
    """

    x = np.log10(x.astype(np.float64))
    y = np.log10(y.astype(np.float64))
    X = np.hstack((np.ones((x.shape[0], 1)), x.reshape((x.shape[0], 1))))

    beta = np.dot(np.linalg.pinv(np.dot(X.transpose(), X)), X.transpose())
    beta = np.dot(beta, y.reshape((y.shape[0], 1)))
    A = np.hstack((np.ones((x.shape[0], 1)), x.reshape((x.shape[0], 1))))

    AtranA = np.dot(A.T, A)

    invAtranA = np.linalg.pinv(AtranA)

    return beta[1][0], beta[0][0]


if __name__ == '__main__':
    cmdLine = argparse.ArgumentParser(
           description='This is part of the PETSc toolkit for evaluating solvers using\n\
                   Time-Accuracy-Size(TAS) spectrum analysis.')

    cmdLine.add_argument('-f', '--file', metavar='<filename>',
                         nargs='*', help='List of files to import for TAS analysis.')

    cmdLine.add_argument('-output_base', '--output_base',
                         default=os.getcwd(), help='Base directory for output.')

    cmdLine.add_argument(
        '-v', '--version', action='version', version='%(prog)s 1.0')

    cmdLine.add_argument('-gfs', '--graph_flops_scaling', type=int, default=0, choices=[0, 1],
                         help='Enables graphing flop rate static scaling graph. Default: %(default)s  do not print the graph. 1 to print the graph.')

    cmdLine.add_argument('-d', '--dim', type=int, default=2, help='Specifies the number of dimensions of the mesh. \
        Default: %(default)s.')

    cmdLine.add_argument('-eg', '--enable_graphs', type=int, default=1, choices=[0, 1],
                         help='Enables graphing. Default: %(default)s  print the graphs. 0 to disable printing the graphs.')

    cmdLine.add_argument('-vv', '--view_variance', type=int, default=0, choices=[0, 1],
                         help='Enables calculating and outputting the Variance. Default: %(default)s does not print the variance. 1 to enable \
        printing the graphs.')

    cmdLine.add_argument('-p', '--problem', default='NULL', help='Enables searching for the names of fields in \
        configureTAS.py. Default: %(default)s does not look for the names.  Instead identifies the fields using \
        a number, 0, 1, 2,...n')

    cmdLine.add_argument('-ts', '--timestep', type=int, default=0, choices=[0, 1],
                         help='Enable if solving a time step problem.')

    cmdLine.add_argument('-dfd', '--setDefaultFileDir', type=str, nargs=1, help='Sets the default path for log \
        files to be processed.')

    cmdLine.add_argument('-dgd', '--setDefaultGraphDir', type=str, nargs=1, help='Sets the default path for graph \
        files to be saved.')

    cmdLine.add_argument('-aefd', '--addEditAliasDir', metavar='<alias> <path>', type=str, nargs='*',
                         help='Add a new alias and path, for log files to be processed or graphs to be saved, or edits an existing one.')

    cmdLine.add_argument('-rad', '--removeAliasDir', metavar='<alias>', type=str, nargs='*',
                         help='Remove an alias and path for log files to be processed or edits an existing one.')

    cmdLine.add_argument('-pad', '--pathAliasData', type=str,
                         nargs=1, help='Specify path alias to use for data.')

    cmdLine.add_argument('-pag', '--pathAliasGraph', type=str,
                         nargs=1, help='Specify path alias to use for data.')

    cmdLine.add_argument('-fl', '--fieldList', type=str,
                         nargs='*', help='List of field names.')

    cmdLineArgs = cmdLine.parse_args()

    main(cmdLineArgs)